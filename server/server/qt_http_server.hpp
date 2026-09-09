#pragma once

#include <QTcpServer>
#include <QThread>
#include <QObject>
#include <vector>
#include <memory>
#include <atomic>

namespace ev {

class QtHttpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit QtHttpServer(QObject* parent = nullptr);
    ~QtHttpServer() override;

    // 启动监听与多线程 Worker 架构
    bool start(const QString& address, quint16 port, int thread_count = 0, bool enable_demo_logging = false);
    void stop();

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    struct WorkerNode {
        QThread* thread{nullptr};
        QObject* context{nullptr};
    };

    std::vector<WorkerNode> workers_;
    std::atomic<size_t> next_worker_index_{0};
    bool is_running_{false};
    bool enable_demo_logging_{false};
};

} // namespace ev
