// SPDX-License-Identifier: GPL-3.0-only
#include "StartOfflineSkinServer.h"

#include "OfflineSkinServer.h"

StartOfflineSkinServer::StartOfflineSkinServer(LaunchTask* parent, AuthSessionPtr session)
    : LaunchStep(parent), m_session(std::move(session))
{}

void StartOfflineSkinServer::executeTask()
{
    if (!m_session || m_session->offline_skin_name.isEmpty() || !m_session->yggdrasil_server.isEmpty()) {
        emitSucceeded();
        return;
    }
    emit logLine(tr("Looking up the Ely.by skin of %1...").arg(m_session->offline_skin_name), MessageLevel::Launcher);
    m_lookup = new ElySkinLookup(this);
    connect(m_lookup, &ElySkinLookup::finished, this, &StartOfflineSkinServer::onLookup);
    m_lookup->start(m_session->offline_skin_name, true);
}

void StartOfflineSkinServer::onLookup(const ElySkinLookup::Result& result)
{
    if (result.kind == ElySkinLookup::Result::NotFound) {
        emit logLine(tr("%1 has no skin on Ely.by, so you play with the default skin.").arg(m_session->offline_skin_name),
                     MessageLevel::Launcher);
        emitSucceeded();
        return;
    }
    if (result.kind == ElySkinLookup::Result::Failed) {
        emit logLine(tr("Couldn't reach Ely.by (%1), so you play with the default skin.").arg(result.error), MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    m_server = new OfflineSkinServer(m_session->uuid, m_session->player_name, result.texturesValue, result.texturesSignature,
                                     result.metadata, this);
    if (!m_server->start()) {
        emit logLine(tr("Couldn't start the local skin server (%1), so you play with the default skin.").arg(m_server->errorString()),
                     MessageLevel::Warning);
        delete m_server;
        emitSucceeded();
        return;
    }
    m_session->yggdrasil_server = m_server->apiRoot();
    m_session->yggdrasil_metadata = m_server->metadataBase64();
    m_session->yggdrasil_optional = true;
    m_session->offline_skin_name = result.elyName;
    emitSucceeded();
}

void StartOfflineSkinServer::finalize()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
    }
}

bool StartOfflineSkinServer::abort()
{
    if (m_lookup)
        m_lookup->abort();
    finalize();
    emitAborted();
    return true;
}
