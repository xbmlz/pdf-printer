#include <QApplication>
#include <QWidget>
#include <QSystemTrayIcon>
#include <QDebug>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QObject>
#include <QUrl>
#include <QDesktopServices>
#include <QThread>
#include <QSettings>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QPrinter>
#include <QPrinterInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPainter>
#include <QScopedPointer>
#include "httplib.h"
#include "poppler-qt5.h"

void writeLog(const QString &message)
{
    // 获取当前日期，格式为 yyyyMMdd
    QString date = QDateTime::currentDateTime().toString("yyyyMMdd");

    // 创建 logs 文件夹（如果不存在）
    QDir logDir("logs");
    if (!logDir.exists())
    {
        if (!logDir.mkpath("."))
        {
            qWarning() << "无法创建 logs 文件夹!";
            return;
        }
    }

    // 构造日志文件路径
    QString logFilePath = logDir.filePath(date + ".log");

    // 打开日志文件（如果文件不存在则会创建）
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        qWarning() << "无法打开日志文件:" << logFilePath;
        return;
    }

    // 创建文本流并写入日志信息
    QTextStream logStream(&logFile);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    logStream << timestamp << " - " << message << "\n";

    // 关闭文件
    logFile.close();
}

// 统一的 JSON 响应：{ "code": 0, "msg": "success", "data": null }
void sendJson(httplib::Response &resp, int httpStatus, int code, const QString &msg,
              const QJsonValue &data = QJsonValue(QJsonValue::Null))
{
    QJsonObject obj;
    obj.insert("code", code);
    obj.insert("msg", msg);
    obj.insert("data", data);

    resp.status = httpStatus;
    resp.set_header("Access-Control-Allow-Origin", "*");
    resp.set_content(QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString(), "application/json");
}

void downloadFile(QString url, QString savePath)
{
    // parse host and port from url
    QUrl parsedUrl(url);
    QString host = parsedUrl.host();
    int port = parsedUrl.port();
    if (port == -1)
    {
        if (parsedUrl.scheme() == "https")
        {
            port = 443;
        }
        else
        {
            port = 80;
        }
    }
    QString path = parsedUrl.path();
    if (parsedUrl.hasQuery())
    {
        path += "?" + parsedUrl.query(QUrl::FullyEncoded);
    }

    writeLog("Downloading file from host " + host + " and port " + QString::number(port) + " with path " + path);
    httplib::Client cli(host.toStdString().c_str(), port);
    auto res = cli.Get(path.toStdString().c_str());
    if (!res)
    {
        throw std::runtime_error("Failed to download file: No response from server");
    }
    if (res->status >= 400)
    {
        throw std::runtime_error("Failed to download file: HTTP " + std::to_string(res->status));
    }
    // save file
    std::ofstream ofs(savePath.toStdString(), std::ios::binary);
    if (!ofs.is_open())
    {
        throw std::runtime_error("Failed to open file for writing");
    }
    ofs.write(res->body.c_str(), res->body.size());
    ofs.close();
}

void handleRoot(const httplib::Request &, httplib::Response &resp)
{
    resp.set_content("This is an print pdf server, build by NGPacs, version 1.0.0", "text/plain");
}

void handlePrint(const httplib::Request &req, httplib::Response &resp)
{
    // 1. 校验参数
    if (!req.has_param("url"))
    {
        sendJson(resp, httplib::StatusCode::BadRequest_400, 1, "url parameter is required");
        return;
    }
    QString url = QString::fromStdString(req.get_param_value("url"));
    QString printerName = req.has_param("printer")
                              ? QString::fromStdString(req.get_param_value("printer"))
                              : QString();
    int copies = req.has_param("copies")
                     ? QString::fromStdString(req.get_param_value("copies")).toInt()
                     : 1;
    if (copies < 1)
    {
        copies = 1;
    }

    // 2. 准备下载目录与临时文件（打印结束后自动清理）
    QDir downloadDir(QDir::currentPath() + "/downloads");
    if (!downloadDir.exists())
    {
        downloadDir.mkpath(".");
    }
    QString uuidStr = QUuid::createUuid().toString().remove(QRegExp("[{}]"));
    QString savePath = downloadDir.absoluteFilePath(uuidStr + ".pdf");

    struct FileGuard
    {
        QString path;
        ~FileGuard()
        {
            if (!path.isEmpty())
            {
                QFile::remove(path);
            }
        }
    } guard{savePath};

    // 3. 下载 PDF
    try
    {
        downloadFile(url, savePath);
    }
    catch (const std::exception &e)
    {
        sendJson(resp, httplib::StatusCode::BadRequest_400, 1, e.what());
        return;
    }

    // 4. 加载 PDF
    QScopedPointer<Poppler::Document> doc(Poppler::Document::load(savePath));
    if (!doc || doc->isLocked())
    {
        sendJson(resp, httplib::StatusCode::BadRequest_400, 1, "Failed to load PDF document");
        return;
    }
    doc->setRenderHint(Poppler::Document::Antialiasing);

    int totalPages = doc->numPages();
    writeLog("PDF document has " + QString::number(totalPages) + " pages");
    if (totalPages == 0)
    {
        sendJson(resp, httplib::StatusCode::BadRequest_400, 1, "PDF document has no pages");
        return;
    }

    // 5. 配置打印机
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::A4);
    printer.setCopyCount(copies);
    if (!printerName.isEmpty())
    {
        QPrinterInfo info = QPrinterInfo::printerInfo(printerName);
        if (info.isNull())
        {
            sendJson(resp, httplib::StatusCode::BadRequest_400, 1, "Printer not found: " + printerName);
            return;
        }
        printer.setPrinterName(printerName);
    }

    // 6. 逐页渲染并打印（单个 QPainter，页间 newPage；用 QImage 避免非 GUI 线程操作 QPixmap）
    QPainter painter;
    if (!painter.begin(&printer))
    {
        sendJson(resp, httplib::StatusCode::InternalServerError_500, 1, "Failed to open printer for output");
        return;
    }

    for (int i = 0; i < totalPages; i++)
    {
        if (i > 0)
        {
            printer.newPage();
        }

        QScopedPointer<Poppler::Page> page(doc->page(i));
        if (!page)
        {
            continue;
        }

        QImage image = page->renderToImage(300, 300);

        QRect rect = painter.viewport();
        QSize size = image.size();
        size.scale(rect.size(), Qt::KeepAspectRatio);

        painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
        painter.setWindow(image.rect());
        painter.drawImage(0, 0, image);
    }
    painter.end();

    writeLog("Print success: " + url + " (printer=" + (printerName.isEmpty() ? "default" : printerName) +
             ", copies=" + QString::number(copies) + ")");
    sendJson(resp, httplib::StatusCode::OK_200, 0, "success");
}

void handleGetPrinters(const httplib::Request &, httplib::Response &resp)
{
    // get system all printers
    QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    QJsonArray printerArray;
    for (const QPrinterInfo &printer : printers)
    {
        QJsonObject printerObj;
        printerObj["name"] = printer.printerName();
        printerObj["status"] = printer.state() == QPrinter::Idle ? "online" : "offline";
        printerObj["isDefault"] = printer.isDefault();
        printerArray.append(printerObj);
    }
    sendJson(resp, httplib::StatusCode::OK_200, 0, "success", printerArray);
}

void startHTTPServer(QString host, int port)
{
    httplib::Server srv;

    srv.Options(R"(\**)", [](const httplib::Request &req, httplib::Response &resp)
                {
        resp.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin").c_str());
        resp.set_header("Allow", "GET, POST, HEAD, OPTIONS");
        resp.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
        resp.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD"); });

    srv.Get("/", handleRoot);
    srv.Get("/print", handlePrint);
    srv.Get("/printers", handleGetPrinters);

    writeLog("Server started at " + host + ":" + QString::number(port));
    srv.listen(host.toStdString(), port);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // load settings
    QSettings settings("config.ini", QSettings::IniFormat);
    QString host = settings.value("host", "0.0.0.0").toString();
    int port = settings.value("port", 8765).toInt();

    // register system tray
    QSystemTrayIcon trayIcon;
    trayIcon.setIcon(QIcon(":/icons/icons/icon.png"));
    trayIcon.setToolTip(QString("NGPacs Print PDF Server"));

    QMenu *trayMenu = new QMenu();

    QAction *openAppDirAction = new QAction("打开程序目录", &trayIcon);
    QObject::connect(openAppDirAction, &QAction::triggered, []()
                     { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); });

    QAction *quitAction = new QAction("退出", &trayIcon);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    trayMenu->addAction(openAppDirAction);
    trayMenu->addAction(quitAction);
    trayIcon.show();
    trayIcon.setContextMenu(trayMenu);

    // start server in another thread
    QThread *serverThread = new QThread();
    QObject::connect(serverThread, &QThread::started, [host, port]()
                     { startHTTPServer(host, port); });
    serverThread->start();

    return app.exec();
}
