#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>
#include <QSet>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        DetailsRole
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    Q_INVOKABLE void renameComputer(int computerIndex, QString name);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);

    // Returns the index of a host that came online before anyone was listening
    // for computerCameOnline (typically because its first poll completed during
    // startup), or -1. Ends the startup phase: later transitions are signalled.
    Q_INVOKABLE int takePendingAutoConnect();

    Q_INVOKABLE QString getComputerName(int computerIndex);

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);

    // Emitted when a paired, supported host transitions to online, including
    // the first time it is seen online after startup.
    void computerCameOnline(int computerIndex, QString name);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    void checkForOnlineTransition(NvComputer* computer);
    bool isEligibleForAutoConnect(NvComputer* computer, QString* name);

    QVector<NvComputer*> m_Computers;
    ComputerManager* m_ComputerManager;
    QHash<QString, NvComputer::ComputerState> m_LastKnownStates;
    QSet<QString> m_PendingAutoConnect;
    bool m_StartupPhase = true;
};
