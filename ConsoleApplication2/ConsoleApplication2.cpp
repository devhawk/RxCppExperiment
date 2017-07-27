// ConsoleApplication2.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using namespace Microsoft::WRL;
using namespace std::chrono_literals;

struct __declspec(uuid("6BDCAAC6-89E9-4CC1-BA48-FEA913F78502")) ISampleCallback : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnComplete(_In_ HRESULT result, uint64_t requestId) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnError(_In_ HRESULT result, uint64_t requestId) = 0;
};

struct __declspec(uuid("A6858B62-1907-494F-9448-EDCFD64025EE")) ISampleClient : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Listen(_In_ ISampleCallback* pCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeRequestAsync(_Out_ uint64_t* requestId) = 0;
};

struct SampleClient : public RuntimeClass<
    RuntimeClassFlags<RuntimeClassType::ClassicCom>,
    ISampleClient>
{
    HRESULT STDMETHODCALLTYPE Listen(_In_ ISampleCallback* pCallback) override
    {
        _callback = pCallback;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MakeRequestAsync(_Out_ uint64_t* requestId) override
    {
        auto cur_request_id = ++_last_request_id;
        *requestId = cur_request_id;
        auto lambda = [this, cur_request_id]()
        {
            std::this_thread::sleep_for(2s);

            if (_callback)
            {
                _callback->OnComplete(S_OK, cur_request_id);
            }
        };

        std::thread(lambda).detach();
        return S_OK;
    }
    
private:
    ComPtr<ISampleCallback> _callback{ nullptr };
    uint64_t _last_request_id{ 0 };
};

struct SampleCallback : public RuntimeClass<
    RuntimeClassFlags<RuntimeClassType::ClassicCom>,
    ISampleCallback>
{
    HRESULT STDMETHODCALLTYPE OnComplete(_In_ HRESULT result, uint64_t requestId) override
    {
        printf("OnComplete %lld %ld\n", requestId, result);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnError(_In_ HRESULT result, uint64_t requestId) override
    {
        printf("OnError %lld %ld\n", requestId, result);
        return S_OK;
    }
};

HRESULT GetSampleClient(ISampleClient ** sample_client)
{
    ComPtr<ISampleClient> sc = Make<SampleClient>();
    *sample_client = sc.Detach();
    return S_OK;
}

void make_request(ComPtr<ISampleClient> & client)
{
    uint64_t request_id;
    HRESULT hr = client->MakeRequestAsync(&request_id);
    if (FAILED(hr)) { throw std::exception("client->MakeRequestAsync", hr); }
    printf("Made Request %lld\n", request_id);
}

int main()
{
    ComPtr<ISampleClient> client;
    HRESULT hr = GetSampleClient(client.GetAddressOf());
    if (FAILED(hr)) { throw std::exception("GetSampleClient", hr); }

    ComPtr<ISampleCallback> callback = Make<SampleCallback>();
    hr = client->Listen(callback.Get());
    if (FAILED(hr)) { throw std::exception("client->Listen", hr); }
    callback.Reset();

    make_request(client);
    make_request(client);
    make_request(client);


    std::cin.get();
}

