// ConsoleApplication2.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using namespace Microsoft::WRL;
using namespace std::chrono_literals;

namespace sample {
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

    HRESULT GetSampleClient(ISampleClient ** sample_client)
    {
        ComPtr<ISampleClient> sc = Make<SampleClient>();
        *sample_client = sc.Detach();
        return S_OK;
    }
}

namespace rxsample {
    struct SampleAdapter
    {
        SampleAdapter()
        {
            HRESULT hr = sample::GetSampleClient(_client.GetAddressOf());
            if (FAILED(hr)) { throw std::exception("GetSampleClient", hr); }

            ComPtr<sample::ISampleCallback> callback = Make<SampleCallback>(_subject.get_subscriber());
            hr = _client->Listen(callback.Get());
            if (FAILED(hr)) { throw std::exception("client->Listen", hr); }
        }

        // Note, this is a hot observable. If the client doesn't subscribe before the message 
        // arrives, they will miss the result. Will fix in later commit.
        rxcpp::observable<bool> MakeRequestAsync(_In_ char const* message)
        {
            uint64_t request_id;
            HRESULT hr = _client->MakeRequestAsync(message, &request_id);
            if (FAILED(hr)) { throw std::exception("client->MakeRequestAsync", hr); }

            return _subject.get_observable()
                .filter([request_id](auto t) { return std::get<0>(t) == request_id; })
                .map([](auto t) { return SUCCEEDED(std::get<1>(t)); });
        }

    private:
        ComPtr<sample::ISampleClient> _client;
        rxcpp::subjects::subject<std::tuple<uint64_t, HRESULT>> _subject;

        struct SampleCallback : public RuntimeClass<
            RuntimeClassFlags<RuntimeClassType::ClassicCom>,
            sample::ISampleCallback>
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
                printf("SampleCallback::OnComplete     %lld %ld\n", requestId, result);
                _subscriber.on_next(std::make_tuple(requestId, result));
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnError(_In_ HRESULT result, uint64_t requestId) override
            {
                printf("SampleCallback::OnError %lld %ld\n", requestId, result);
                _subscriber.on_next(std::make_tuple(requestId, result));
                return S_OK;
            }

            rxcpp::subscriber<std::tuple<uint64_t, HRESULT>> _subscriber;
        };
    };
}

void write_result(char const* message, bool result)
{
    static HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO buffer_info;
    GetConsoleScreenBufferInfo(hOut, &buffer_info);
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("write_result                   %s %s\n", result ? "true " : "false", message);
    SetConsoleTextAttribute(hOut, buffer_info.wAttributes);
}

int main()
{
    rxsample::SampleAdapter adapter;
    adapter.MakeRequestAsync("foo").subscribe([](bool f) { write_result("foo", f); });
    adapter.MakeRequestAsync("bar").subscribe([](bool f) { write_result("bar", f); });
    adapter.MakeRequestAsync("baz").subscribe([](bool f) { write_result("baz", f); });

    std::cin.get();
}

