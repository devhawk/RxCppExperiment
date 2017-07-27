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
    virtual HRESULT STDMETHODCALLTYPE Listen(_In_ ISampleCallback* callback) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeRequestAsync(_In_ char const* message, _Out_ uint64_t* requestId) = 0;
};

struct SampleClient : public RuntimeClass<
    RuntimeClassFlags<RuntimeClassType::ClassicCom>,
    ISampleClient>
{
    HRESULT STDMETHODCALLTYPE Listen(_In_ ISampleCallback* callback) override
    {
        _callback = callback;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MakeRequestAsync(_In_ char const* message, _Out_ uint64_t* requestId) override
    {
        auto cur_request_id = ++_last_request_id;
        printf("SampleClient::MakeRequestAsync %lld %s\n", cur_request_id, message);

        *requestId = cur_request_id;
        auto lambda = [this, cur_request_id]()
        {
            std::this_thread::sleep_for(cur_request_id * 1s);

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
    SampleCallback(rxcpp::subscriber<std::tuple<uint64_t, HRESULT>> subscriber)
        : _subscriber(subscriber)
    {
    }

private:
    ~SampleCallback()
    {
        _subscriber.on_completed();
    }

    HRESULT STDMETHODCALLTYPE OnComplete(_In_ HRESULT result, uint64_t requestId) override
    {
        printf("SampleCallback::OnComplete %lld %ld\n", requestId, result);
        _subscriber.on_next(std::make_tuple(requestId, result));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnError(_In_ HRESULT result, uint64_t requestId) override
    {
        printf("SampleCallback::OnError %lld %ld\n", requestId, result);
        _subscriber.on_next(std::make_tuple(requestId, result));
        return S_OK;
    }

    // I tried both the current approach (passing the subscriber in via the ctor) as well
    // as having the subject inside the callback. Since the callback is an internal implementation
    // detail of the adapter, I think it's better off this way and letting the adapter own the 
    // subject. Will have to see what happens when the subject is torn down before the subscriber
    // but I'll need to build out the adapter to see. I think the subscription sticks around as 
    // long as someone has a reference to it somewhere.
    rxcpp::subscriber<std::tuple<uint64_t, HRESULT>> _subscriber;
};

HRESULT GetSampleClient(ISampleClient ** sample_client)
{
    ComPtr<ISampleClient> sc = Make<SampleClient>();
    *sample_client = sc.Detach();
    return S_OK;
}

void make_request(ComPtr<ISampleClient> & client, char const* message)
{
    uint64_t request_id;
    HRESULT hr = client->MakeRequestAsync(message, &request_id);
    if (FAILED(hr)) { throw std::exception("client->MakeRequestAsync", hr); }
}

int main()
{
    ComPtr<ISampleClient> client;
    HRESULT hr = GetSampleClient(client.GetAddressOf());
    if (FAILED(hr)) { throw std::exception("GetSampleClient", hr); }

    {
        rxcpp::subjects::subject<std::tuple<uint64_t, HRESULT>> subject;

        ComPtr<ISampleCallback> callback = Make<SampleCallback>(subject.get_subscriber());
        hr = client->Listen(callback.Get());
        if (FAILED(hr)) { throw std::exception("client->Listen", hr); }
        callback.Reset();

        subject.get_observable().subscribe([](std::tuple<uint64_t, HRESULT> t)
        {
            printf("OnNext %lld %ld\n", std::get<0>(t), std::get<1>(t));
        });
    }

    make_request(client, "foo");
    make_request(client, "bar");
    make_request(client, "baz");

    std::cin.get();
}

