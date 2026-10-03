#include "app/BackendClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSharedPointer>
#include <QStandardPaths>

namespace mcsm {
namespace {

const char *kBackendName = "mcsm-cli";

QString withExtension(const QString &path)
{
#ifdef Q_OS_WIN
    if (!path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        return path + QStringLiteral(".exe");
#endif
    return path;
}

QStringList candidatePaths()
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("MCSM_BACKEND");
    if (!env.isEmpty())
        candidates << env;

    QDir dir(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 5; ++level) {
        candidates << withExtension(dir.filePath(QLatin1String(kBackendName)));
        candidates << withExtension(QDir(dir.filePath(QStringLiteral("backend")))
                                        .filePath(QLatin1String(kBackendName)));
        candidates << withExtension(QDir(dir.filePath(QStringLiteral("bin")))
                                        .filePath(QLatin1String(kBackendName)));
        candidates << withExtension(QDir(dir.filePath(QStringLiteral("build/bin")))
                                        .filePath(QLatin1String(kBackendName)));
        candidates << withExtension(QDir(dir.filePath(QStringLiteral("build")))
                                        .filePath(QDir(QStringLiteral("backend"))
                                                      .filePath(QLatin1String(kBackendName))));
        if (!dir.cdUp())
            break;
    }
    const QString pathBinary = QStandardPaths::findExecutable(QLatin1String(kBackendName));
    if (!pathBinary.isEmpty())
        candidates << pathBinary;
    return candidates;
}

/// Accumulates backend output and separates NDJSON progress events from the
/// final result envelope.
struct StreamState
{
    QByteArray buffer;
    QJsonObject envelope;
    bool envelopeFound = false;
    QStringList rawLines;
};

void consumeChunk(StreamState *state, const QByteArray &chunk, const BackendClient::ProgressHandler &progress)
{
    if (!state)
        return;
    state->buffer.append(chunk);
    int index = state->buffer.indexOf('\n');
    while (index >= 0) {
        const QByteArray line = state->buffer.left(index);
        state->buffer.remove(0, index + 1);
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty())
            state->rawLines << text;
        if (text.startsWith(QLatin1Char('{'))) {
            QJsonParseError error {};
            const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject object = doc.object();
                if (object.value(QStringLiteral("type")).toString() == QLatin1String("progress")) {
                    if (progress) {
                        progress(object.value(QStringLiteral("stage")).toString(),
                                 object.value(QStringLiteral("percent")).toInt(),
                                 object.value(QStringLiteral("detail")).toString());
                    }
                } else if (object.contains(QStringLiteral("ok"))) {
                    state->envelope = object;
                    state->envelopeFound = true;
                }
            }
        }
        index = state->buffer.indexOf('\n');
    }
}

Reply buildReply(const StreamState &state, const QString &stderrText, int exitCode)
{
    Reply reply;
    QStringList all = state.rawLines;
    if (!stderrText.trimmed().isEmpty())
        all << stderrText.trimmed();
    reply.rawOutput = all.join(QLatin1Char('\n'));

    if (!state.envelopeFound) {
        reply.code = QStringLiteral("BACKEND_PROTOCOL_ERROR");
        reply.message = exitCode == 0
                            ? QStringLiteral("后端没有返回可解析的结果")
                            : QStringLiteral("后端命令执行失败（退出码 %1）").arg(exitCode);
        if (reply.rawOutput.isEmpty())
            reply.detail = QStringLiteral("没有收到任何输出，请确认 mcsm-cli 可以独立运行。");
        return reply;
    }

    reply.ok = state.envelope.value(QStringLiteral("ok")).toBool();
    reply.data = state.envelope.value(QStringLiteral("data")).toObject();
    if (reply.ok) {
        const QJsonArray warnings = state.envelope.value(QStringLiteral("warnings")).toArray();
        for (const QJsonValue &warning : warnings) {
            if (warning.isString())
                reply.warnings << warning.toString();
        }
        return reply;
    }

    const QJsonObject error = state.envelope.value(QStringLiteral("error")).toObject();
    reply.code = error.value(QStringLiteral("code")).toString();
    reply.message = error.value(QStringLiteral("message")).toString();
    reply.detail = error.value(QStringLiteral("detail")).toString();
    return reply;
}

} // namespace

QString Reply::errorText() const
{
    if (ok)
        return QString();
    QString text = message.isEmpty() ? QStringLiteral("后端命令执行失败") : message;
    if (!code.isEmpty())
        text = QStringLiteral("[%1] %2").arg(code, text);
    if (!detail.trimmed().isEmpty())
        text += QStringLiteral("\n") + detail.trimmed();
    else if (!rawOutput.trimmed().isEmpty())
        text += QStringLiteral("\n") + rawOutput.trimmed();
    return text;
}

BackendClient::BackendClient(QObject *parent)
    : QObject(parent)
{
    m_executable = locateBackend();
}

QString BackendClient::locateBackend()
{
    const QStringList candidates = candidatePaths();
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }
    return QString();
}

QString BackendClient::siblingBackend()
{
    const QString path = withExtension(
        QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kBackendName)));
    return QFileInfo::exists(path) ? QFileInfo(path).absoluteFilePath() : QString();
}

void BackendClient::setExecutable(const QString &path)
{
    if (m_executable == path)
        return;
    m_executable = path;
    emit availabilityChanged(isConfigured());
}

void BackendClient::setDataHome(const QString &home)
{
    m_dataHome = home;
}

bool BackendClient::isConfigured() const
{
    return !m_executable.isEmpty() && QFileInfo::exists(m_executable);
}

QStringList BackendClient::globalFlags() const
{
    QStringList flags;
    if (!m_dataHome.isEmpty())
        flags << QStringLiteral("--home") << m_dataHome;
    return flags;
}

void BackendClient::request(const QStringList &arguments,
                            QObject *context,
                            ResultHandler handler,
                            ProgressHandler progress)
{
    if (!isConfigured()) {
        Reply reply;
        reply.code = QStringLiteral("BACKEND_MISSING");
        reply.message = QStringLiteral("未找到后端程序 mcsm-cli");
        reply.detail = QStringLiteral("请先编译 backend 目标，或在“设置”中手动指定 mcsm-cli 的路径。");
        if (handler)
            handler(reply);
        return;
    }

    auto *process = new QProcess(context ? context : this);
    process->setProgram(m_executable);
    process->setArguments(globalFlags() + arguments);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_active.insert(process);
    emit commandStarted(arguments);

    const QStringList args = arguments;
    QPointer<QProcess> guard(process);
    QSharedPointer<StreamState> state = QSharedPointer<StreamState>::create();

    QObject::connect(process, &QProcess::readyReadStandardOutput, process,
                     [guard, state, progress]() {
                         if (!guard)
                             return;
                         consumeChunk(state.data(), guard->readAllStandardOutput(), progress);
                     });

    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process,
                     [this, guard, state, args, handler, progress](int exitCode, QProcess::ExitStatus) {
                         if (!guard)
                             return;
                         consumeChunk(state.data(), guard->readAllStandardOutput(), progress);
                         const QString stderrText = QString::fromUtf8(guard->readAllStandardError());
                         m_active.remove(guard.data());
                         emit commandFinished(args, exitCode == 0);
                         const Reply reply = buildReply(*state, stderrText, exitCode);
                         if (handler)
                             handler(reply);
                         guard->deleteLater();
                     });

    QObject::connect(process, &QProcess::errorOccurred, process,
                     [this, guard, args, handler](QProcess::ProcessError error) {
                         if (!guard || error != QProcess::FailedToStart)
                             return;
                         m_active.remove(guard.data());
                         Reply reply;
                         reply.code = QStringLiteral("BACKEND_FAILED");
                         reply.message = QStringLiteral("无法启动后端进程");
                         reply.detail = guard->errorString();
                         emit commandFinished(args, false);
                         if (handler)
                             handler(reply);
                         guard->deleteLater();
                     });

    process->start();
}

Reply BackendClient::requestSync(const QStringList &arguments, int timeoutMs)
{
    Reply reply;
    if (!isConfigured()) {
        reply.code = QStringLiteral("BACKEND_MISSING");
        reply.message = QStringLiteral("未找到后端程序 mcsm-cli");
        return reply;
    }

    QProcess process;
    process.setProgram(m_executable);
    process.setArguments(globalFlags() + arguments);
    process.start();
    if (!process.waitForStarted(10000)) {
        reply.code = QStringLiteral("BACKEND_FAILED");
        reply.message = QStringLiteral("无法启动后端进程");
        reply.detail = process.errorString();
        return reply;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        reply.code = QStringLiteral("BACKEND_TIMEOUT");
        reply.message = QStringLiteral("后端命令超时");
        return reply;
    }

    StreamState state;
    consumeChunk(&state, process.readAllStandardOutput(), ProgressHandler());
    consumeChunk(&state, QByteArray("\n"), ProgressHandler());
    const QString stderrText = QString::fromUtf8(process.readAllStandardError());
    return buildReply(state, stderrText, process.exitCode());
}

QProcess *BackendClient::streamLogs(const QString &serverId, int tail, QObject *context, LineHandler onLine)
{
    if (!isConfigured())
        return nullptr;

    auto *process = new QProcess(context ? context : this);
    process->setProgram(m_executable);
    process->setArguments(globalFlags()
                          + QStringList {QStringLiteral("server"), QStringLiteral("logs"),
                                         QStringLiteral("--id"), serverId,
                                         QStringLiteral("--tail"), QString::number(tail),
                                         QStringLiteral("--follow")});
    process->setProcessChannelMode(QProcess::MergedChannels);

    auto buffer = QSharedPointer<QByteArray>::create();
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process, buffer, onLine]() {
        buffer->append(process->readAllStandardOutput());
        int index = buffer->indexOf('\n');
        while (index >= 0) {
            const QByteArray raw = buffer->left(index);
            buffer->remove(0, index + 1);
            const QString line = QString::fromUtf8(raw).trimmed();
            if (!line.isEmpty() && onLine) {
                if (line.startsWith(QLatin1Char('{'))) {
                    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
                    if (doc.isObject()) {
                        onLine(doc.object());
                    } else {
                        QJsonObject raw2;
                        raw2.insert(QStringLiteral("type"), QStringLiteral("log"));
                        raw2.insert(QStringLiteral("line"), line);
                        onLine(raw2);
                    }
                } else {
                    QJsonObject raw2;
                    raw2.insert(QStringLiteral("type"), QStringLiteral("log"));
                    raw2.insert(QStringLiteral("line"), line);
                    onLine(raw2);
                }
            }
            index = buffer->indexOf('\n');
        }
    });

    process->start();
    return process;
}

} // namespace mcsm
