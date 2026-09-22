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
        // Search the Spout name list.
        //
        // These locals must be initialized. CheckSender() only assigns the
        // handle and format when it finds a live sender; on failure it zeroes
        // the width and height but leaves the handle and format untouched.
        unsigned int width = 0, height = 0;
        HANDLE handle = nullptr;
        DWORD format = 0;
        auto res = _system->spout
          .CheckSender(_name.c_str(), width, height, handle, format);

        // The sender isn't available: release the current texture and wait for
        // it to appear. This is not an error condition - a receiver commonly
        // outlives its sender, or is created before the sender exists.
        //
        // Falling through to the share-handle open below with an unset handle
        // is undefined behaviour and crashes the D3D runtime.
        if (!res || handle == nullptr)
        {
            _texture = nullptr;
            _width = 0;
            _height = 0;
            _format = Format::Unknown;
            return;
        }

        // Do nothing further if the current texture is valid.
        if (_texture && _width == width && _height == height) return;

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

    std::string _name;
    unsigned int _width = 0, _height = 0;
    Format _format = Format::Unknown;
    WRL::ComPtr<IUnknown> _texture;
};

} // namespace KlakSpout
