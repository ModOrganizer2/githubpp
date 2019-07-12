#include <QJsonDocument>
#include <QEventLoop>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include "github.h"

#include <QThread>
#include <QCoreApplication>

static const QString GITHUB_URL("https://api.github.com");
static const QString USER_AGENT("GitHubPP");


GitHub::GitHub(const char *clientId)
  : m_AccessManager(new QNetworkAccessManager(this))
{
  if (m_AccessManager->networkAccessible()
      == QNetworkAccessManager::UnknownAccessibility) {
    m_AccessManager->setNetworkAccessible(QNetworkAccessManager::Accessible);
  }
}

GitHub::~GitHub()
{
  // delete all the replies since they depend on the access manager, which is
  // about to be deleted
  for (auto* reply : m_replies) {
    reply->disconnect();
    delete reply;
  }
}

QJsonArray GitHub::releases(const Repository &repo)
{
  QJsonDocument result
      = request(Method::GET,
                QString("repos/%1/%2/releases").arg(repo.owner, repo.project),
                QByteArray(),
                true);
  return result.array();
}

void GitHub::releases(const Repository &repo,
                      const std::function<void(const QJsonArray &)> &callback)
{
  request(Method::GET,
          QString("repos/%1/%2/releases").arg(repo.owner, repo.project),
          QByteArray(), [callback](const QJsonDocument &result) {
            callback(result.array());
          }, true);
}

QJsonDocument GitHub::handleReply(QNetworkReply *reply)
{
  int statusCode
      = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (statusCode != 200) {
    return QJsonDocument(QJsonObject(
        {{"http_status", statusCode},
         {"redirection",
          reply->attribute(QNetworkRequest::RedirectionTargetAttribute)
              .toString()},
         {"reason", reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)
                        .toString()}}));
  }

  QByteArray data = reply->readAll();
  if (data.isNull() || data.isEmpty()
      || (strcmp(data.constData(), "null") == 0)) {
    return QJsonDocument();
  }

  QJsonParseError parseError;
  QJsonDocument result = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    return QJsonDocument(
        QJsonObject({{"parse_error", parseError.errorString()}}));
  }

  return result;
}

QNetworkReply *GitHub::genReply(Method method, const QString &path,
                                const QByteArray &data, bool relative)
{
  QNetworkRequest request(relative ? GITHUB_URL + "/" + path : path);

  request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
  request.setRawHeader("Accept", "application/vnd.github.v3+json");

  switch (method) {
    case Method::GET:
      return m_AccessManager->get(request);
    case Method::POST:
      return m_AccessManager->post(request, data);
    default:
      // this shouldn't be possible as all enum options are handled
      throw std::runtime_error("invalid method");
  }
}

QJsonDocument GitHub::request(Method method, const QString &path,
                              const QByteArray &data, bool relative)
{
  QEventLoop wait;
  QNetworkReply *reply = genReply(method, path, data, relative);

  connect(reply, SIGNAL(finished), &wait, SLOT(quit()));
  wait.exec();
  QJsonDocument result = handleReply(reply);
  reply->deleteLater();

  QJsonObject object = result.object();
  if (object.value("http_status").toDouble() == 301.0) {
    return request(method, object.value("redirection").toString(), data, false);
  } else {
    return result;
  }
}

void GitHub::request(Method method, const QString &path, const QByteArray &data,
                     const std::function<void(const QJsonDocument &)> &callback,
                     bool relative)
{
  // make sure the timer is a child of this so it's deleted correctly and
  // doesn't fire after the GitHub object is destroyed; this happens when
  // restarting MO by switching instances, for example
  QTimer *timer = new QTimer(this);

  timer->setSingleShot(true);
  timer->setInterval(30000);
  QNetworkReply *reply = genReply(method, path, data, relative);

  // remember this reply
  m_replies.push_back(reply);

  connect(reply, &QNetworkReply::finished, [this, reply, timer, method, data, callback]() {
    QJsonDocument result = handleReply(reply);
    QJsonObject object = result.object();
    timer->stop();
    if (object.value("http_status").toDouble() == 301.0) {
      request(method, object.value("redirection").toString(), data, callback,
              false);
    } else {
      callback(result);
    }

    deleteReply(reply);
  });

  connect(reply,
          static_cast<void (QNetworkReply::*)(QNetworkReply::NetworkError)>(
              &QNetworkReply::error),
          [this, reply, timer, callback](QNetworkReply::NetworkError error) {
            qDebug("network error %d", error);
            timer->stop();
            reply->disconnect();
            callback(QJsonDocument(
                QJsonObject({{"network_error", reply->errorString()}})));

            deleteReply(reply);
          });

  connect(timer, &QTimer::timeout, [this, reply]() {
    qDebug("timeout");

    // don't delete the reply, abort will fire the error() handler above
    reply->abort();
  });

  timer->start();
}

void GitHub::deleteReply(QNetworkReply* reply)
{
  // remove from the list
  auto itor = std::find(m_replies.begin(), m_replies.end(), reply);
  if (itor != m_replies.end()) {
    m_replies.erase(itor);
  }

  // delete
  reply->deleteLater();
}
