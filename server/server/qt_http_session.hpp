#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QtCore/qglobal.h>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using qsizetype = int;
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>

namespace ev {

namespace beast = boost::beast;
namespace http = beast::http;

template <bool EnableLogging = false>
class QtHttpSession : public QObject {
public:
    explicit QtHttpSession(qintptr socketDescriptor, QObject* parent = nullptr);
    ~QtHttpSession() override;

    void onReadyRead();
    void onDisconnected();
    void on_async_response_ready(std::string res_bytes, bool keep_alive);

private:
    void process_pipeline();

    QTcpSocket* socket_{nullptr};
    std::unique_ptr<http::request_parser<http::string_body>> parser_;
    QByteArray raw_buffer_;
    bool is_processing_{false};
};

} // namespace ev
