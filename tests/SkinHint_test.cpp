// SPDX-License-Identifier: GPL-3.0-only
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "minecraft/launch/SkinHint.h"

namespace {
int readVarInt(const QByteArray& data, int& pos)
{
    int value = 0;
    int shift = 0;
    while (pos < data.size() && shift < 35) {
        const auto byte = static_cast<quint8>(data.at(pos++));
        value |= (byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return value;
        shift += 7;
    }
    return -1;
}
}  // namespace

class SkinHintTest : public QObject {
    Q_OBJECT
   private slots:
    // The hint must be an ordinary server list ping with "\0EMC-SKIN\0<player>\0<ely name>" after the address.
    void test_handshakeCarriesHint()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        SkinHint::sendAll(nullptr, "EmcTest_1", "Savi_oni", QString("127.0.0.1:%1").arg(server.serverPort()));

        QVERIFY(server.waitForNewConnection(5000));
        QTcpSocket* socket = server.nextPendingConnection();
        QByteArray data;
        QTRY_VERIFY_WITH_TIMEOUT((data += socket->readAll(), data.size() > 20 && data.endsWith(QByteArray("\x01\x00", 2))), 5000);

        int pos = 0;
        const int length = readVarInt(data, pos);
        QVERIFY(length > 0);
        QCOMPARE(readVarInt(data, pos), 0x00);  // handshake
        QVERIFY(readVarInt(data, pos) > 0);     // protocol
        const int hostLength = readVarInt(data, pos);
        const QByteArray host = data.mid(pos, hostLength);
        pos += hostLength + 2;                  // the address, then the port
        QCOMPARE(readVarInt(data, pos), 1);     // next state: status
        QCOMPARE(host, QByteArray("127.0.0.1\0EMC-SKIN\0EmcTest_1\0Savi_oni", 37));
    }

    // Nothing is sent without both names.
    void test_nothingWithoutNames()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        SkinHint::sendAll(nullptr, "EmcTest_1", QString(), QString("127.0.0.1:%1").arg(server.serverPort()));
        QVERIFY(!server.waitForNewConnection(1500));
    }

    // Optional live check: EMC_SKINHINT_LIVE=<server address> sends a real hint and leaves time for it to land.
    void test_live()
    {
        const QString live = qEnvironmentVariable("EMC_SKINHINT_LIVE");
        if (live.isEmpty())
            QSKIP("set EMC_SKINHINT_LIVE to send a real hint");
        SkinHint::sendAll(nullptr, "EmcTest_1", "Savi_oni", live);
        QTest::qWait(6000);
    }
};

QTEST_GUILESS_MAIN(SkinHintTest)

#include "SkinHint_test.moc"
