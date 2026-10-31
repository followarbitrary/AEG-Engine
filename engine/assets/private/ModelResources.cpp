#include <ModelResources.hpp>
#include <ModelImporter.hpp>
#include <Utf8Path.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace aeg::assets {
struct ModelResources::Impl {
    struct Entry {
        ModelHandle handle{};
        std::filesystem::path path;
        ModelLoadState state{ModelLoadState::Invalid};
        std::shared_ptr<const ModelData> data;
        std::string error;
    };
    struct Request {
        ModelHandle handle{};
        std::filesystem::path path;
    };

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::unordered_map<std::string, ModelHandle> byPath;
    std::unordered_map<ModelHandle, Entry> entries;
    std::deque<Request> requests;
    std::deque<ModelCompletion> completions;
    ModelHandle nextHandle{1};
    std::jthread worker;

    static std::filesystem::path normalize(const std::filesystem::path& input) {
        std::error_code error;
        auto path = std::filesystem::absolute(input, error).lexically_normal();
        if (error || path.empty()) return {};
        return path;
    }

    void run(std::stop_token stop) {
        while (true) {
            Request request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, stop, [&] { return !requests.empty(); });
                if (requests.empty()) return;
                request = std::move(requests.front());
                requests.pop_front();
                entries.at(request.handle).state = ModelLoadState::Loading;
            }
            ModelCompletion completion;
            completion.handle = request.handle;
            completion.path = request.path;
            try {
                completion.data = std::make_shared<ModelData>(loadModel(request.path));
                completion.state = ModelLoadState::Ready;
            } catch (const std::exception& error) {
                completion.state = ModelLoadState::Failed;
                completion.error = error.what();
            } catch (...) {
                completion.state = ModelLoadState::Failed;
                completion.error = "unknown model load failure";
            }
            std::lock_guard lock(mutex);
            auto& entry = entries.at(request.handle);
            entry.state = completion.state;
            entry.data = completion.data;
            entry.error = completion.error;
            completions.push_back(std::move(completion));
        }
    }
};

ModelResources::ModelResources() : impl_(std::make_unique<Impl>()) {
    impl_->worker = std::jthread([impl = impl_.get()](std::stop_token stop) { impl->run(stop); });
}

ModelResources::~ModelResources() {
    impl_->worker.request_stop();
    impl_->wake.notify_all();
}

ModelHandle ModelResources::request(const std::filesystem::path& input, const bool forceReload) {
    const auto path = Impl::normalize(input);
    const auto key = foundation::pathToUtf8(path);
    std::lock_guard lock(impl_->mutex);
    if (!forceReload && !key.empty()) {
        if (const auto found = impl_->byPath.find(key); found != impl_->byPath.end()) {
            const auto& entry = impl_->entries.at(found->second);
            if (entry.state == ModelLoadState::Ready || entry.state == ModelLoadState::Failed)
                impl_->completions.push_back({entry.handle, entry.path, entry.state, entry.data, entry.error});
            return entry.handle;
        }
    }
    const auto handle = impl_->nextHandle++;
    Impl::Entry entry{handle, path, path.empty() ? ModelLoadState::Failed : ModelLoadState::Queued, {},
                      path.empty() ? "invalid model path" : std::string{}};
    impl_->entries.emplace(handle, entry);
    if (!key.empty()) impl_->byPath[key] = handle;
    if (path.empty()) {
        impl_->completions.push_back({handle, input, ModelLoadState::Failed, {}, entry.error});
        return handle;
    }
    impl_->requests.push_back({handle, path});
    impl_->wake.notify_one();
    return handle;
}

ModelLoadState ModelResources::state(const ModelHandle handle) const {
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->entries.find(handle);
    return found == impl_->entries.end() ? ModelLoadState::Invalid : found->second.state;
}

std::vector<ModelCompletion> ModelResources::drainCompleted() {
    std::lock_guard lock(impl_->mutex);
    std::vector<ModelCompletion> result;
    result.reserve(impl_->completions.size());
    while (!impl_->completions.empty()) {
        result.push_back(std::move(impl_->completions.front()));
        impl_->completions.pop_front();
    }
    return result;
}

} // namespace aeg::assets
