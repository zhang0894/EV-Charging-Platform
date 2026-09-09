#include "qt_http_server.hpp"
#include "qt_http_session.hpp"
#include <QHostAddress>
#include <thread>
#include <iostream>

namespace ev {

QtHttpServer::QtHttpServer(QObject* parent)
    : QTcpServer(parent) {
}

QtHttpServer::~QtHttpServer() {
    stop();
}

bool QtHttpServer::start(const QString& address, quint16 port, int thread_count, bool enable_demo_logging) {
    if (is_running_) return true;
    enable_demo_logging_ = enable_demo_logging;

    // 确定工作线程数 (默认 hardware_concurrency，至少 2 线程)
    int concurrency = (thread_count <= 0)
        ? static_cast<int>(std::max(2u, std::thread::hardware_concurrency()))
        : thread_count;

    workers_.reserve(concurrency);
    for (int i = 0; i < concurrency; ++i) {
        auto* thread = new QThread(this);
        auto* ctx = new QObject();
        ctx->moveToThread(thread);
        thread->start();
        workers_.push_back({thread, ctx});
    }

    QHostAddress host_addr = (address == "0.0.0.0") ? QHostAddress::Any : QHostAddress(address);
    if (!listen(host_addr, port)) {
        std::cerr << "[QtHttpServer] 无法监听端口 " << port << ": "
                  << errorString().toStdString() << "\n";
        stop();
        return false;
    }

    is_running_ = true;
    std::cout << "[QtHttpServer] 成功启动多线程 Qt 网络引擎 (" << concurrency
              << " 个并发 Worker 线程)，监听于 " << address.toStdString()
              << ":" << port << "\n";
    return true;
}

void QtHttpServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    close();

    for (auto& node : workers_) {
        if (node.thread) {
            node.thread->quit();
            node.thread->wait();
            delete node.context;
            delete node.thread;
        }
    }
    workers_.clear();
}

void QtHttpServer::incomingConnection(qintptr socketDescriptor) {
    if (workers_.empty()) {
        // 单线程回退模式
        if (enable_demo_logging_) {
            new QtHttpSession<true>(socketDescriptor, this);
        } else {
            new QtHttpSession<false>(socketDescriptor, this);
        }
        return;
    }

    // 负载均衡分发至 Worker 线程
    size_t idx = next_worker_index_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    auto& target_worker = workers_[idx];

    if (enable_demo_logging_) {
        QMetaObject::invokeMethod(target_worker.context, [socketDescriptor, ctx = target_worker.context]() {
            new QtHttpSession<true>(socketDescriptor, ctx);
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(target_worker.context, [socketDescriptor, ctx = target_worker.context]() {
            new QtHttpSession<false>(socketDescriptor, ctx);
        }, Qt::QueuedConnection);
    }
}

} // namespace ev
