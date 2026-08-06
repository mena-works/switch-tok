#include "task.hpp"

#include <memory>
#include <pthread.h>

#include <borealis.hpp>

namespace
{

constexpr size_t kStackBytes = 512 * 1024;

} // namespace

void runDetached(std::function<void()> body)
{
    auto* payload = new std::function<void()>(std::move(body));

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStackBytes);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t tid;
    const int rc = pthread_create(
        &tid, &attr,
        [](void* arg) -> void* {
            std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()>*>(arg));
            (*fn)();
            return nullptr;
        },
        payload);

    pthread_attr_destroy(&attr);

    if (rc != 0)
    {
        brls::Logger::error("pthread_create failed: {}", rc);
        delete payload;
    }
}
