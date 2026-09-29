#pragma once

#include "Common.h"
#include "System.h"
#include "Format.h"

namespace KlakSpout {

// DX11/12 compatible Spout receiver class
class Receiver final
{
public:

    Receiver(const char* name)
      : _name(name) {}

    ~Receiver()
    {
        _texture = nullptr;
    }

    void update()
    {
        // Look the sender up by its own info block, not through the shared
        // name list.
        //
        // CheckSender() must not be used here. It requires the name to be in
        // the "SpoutSenderNames" list, and when reading the sender's info block
        // fails it calls ReleaseSenderName() - deleting a *live* sender from
        // the list for every Spout app on the machine. That read can fail
        // while the sender is alive: SpoutSharedMemory::Lock() gives up after
        // 67 ms, and a sender that is being released and re-registered has no
        // info block for a moment. A receiver polling every frame gets many
        // chances to hit either, evicts the sender, and then cannot reconnect
        // to it because it is no longer listed. FindSender() reads the info
        // block directly and never writes the list.
        //
        // These locals must be initialized: on failure FindSender() leaves
        // all outputs untouched.
        unsigned int width = 0, height = 0;
        HANDLE handle = nullptr;
        DWORD format = 0;
        char name[SpoutMaxSenderNameLen] = {};
        strncpy_s(name, _name.c_str(), _TRUNCATE);
        auto res = name[0] != 0 && _system->spout
          .FindSender(name, width, height, handle, format);

        // The sender isn't available. A single failed read is usually the
        // info mutex timing out, not the sender going away, so keep the
        // current texture through short gaps instead of dropping it.
        //
        // Falling through to the share-handle open below with an unset handle
        // is undefined behaviour and crashes the D3D runtime.
        if (!res || handle == nullptr || width == 0 || height == 0)
        {
            if (_misses < MissTolerance && ++_misses < MissTolerance) return;
            _texture = nullptr;
            _handle = nullptr;
            _width = 0;
            _height = 0;
            _format = Format::Unknown;
            return;
        }

        _misses = 0;

        // Do nothing further if the current texture is valid. The handle is
        // compared too: a sender can recreate its texture at the same size.
        if (_texture && _handle == handle &&
            _width == width && _height == height) return;

        HRESULT hres;

        if (_system->isD3D12)
        {
            // Handle -> D3D12Resource
            WRL::ComPtr<ID3D12Resource> resource;
            hres = _system->getD3D12Device()
              ->OpenSharedHandle(handle, IID_PPV_ARGS(&resource));
            _texture = resource;
        }
        else
        {
            // Handle -> D3D11Resource
            WRL::ComPtr<ID3D11Resource> resource;
            hres = _system->getD3D11Device()
              ->OpenSharedResource(handle, IID_PPV_ARGS(&resource));
            _texture = resource;
        }

        _handle = SUCCEEDED(hres) ? handle : nullptr;  // retry next frame on failure
        _width = width;
        _height = height;
        _format = ToFormat(static_cast<DXGI_FORMAT>(format));

        if (FAILED(hres)) LogError("OpenSharedResource", _name, hres);
    }

    // Receiver interop data structure
    // Should match with Klak.Spout.Plugin.ReceiverData (Plugin.cs)
    struct InteropData
    {
        unsigned int width, height;
        Format format;
        void* texture_pointer;
    };

    InteropData getInteropData() const
    {
        return InteropData
          { .width = _width, .height = _height, .format = _format,
            .texture_pointer = _texture.Get() };
    }

private:

    // Consecutive failed lookups (about one second of frames) before the
    // sender is treated as gone and the texture is released.
    static constexpr int MissTolerance = 60;

    std::string _name;
    unsigned int _width = 0, _height = 0;
    Format _format = Format::Unknown;
    HANDLE _handle = nullptr;
    int _misses = 0;
    WRL::ComPtr<IUnknown> _texture;
};

} // namespace KlakSpout
