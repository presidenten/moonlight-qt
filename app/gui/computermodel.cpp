#include "computermodel.h"

#include <QThreadPool>

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);

    m_Computers = m_ComputerManager->getComputers();

    // Snapshot the current state of each host so we only react to
    // transitions we observe. Hosts are CS_UNKNOWN until first polled, but
    // polling may already have completed before this model was created (it
    // starts as soon as the window is active), so a host that is already
    // online here is remembered as pending and picked up by the PC view.
    for (NvComputer* computer : std::as_const(m_Computers)) {
        NvComputer::ComputerState state;
        {
            QReadLocker lock(&computer->lock);
            state = computer->state;
        }
        m_LastKnownStates[computer->uuid] = state;

        QString name;
        if (state == NvComputer::CS_ONLINE && isEligibleForAutoConnect(computer, &name)) {
            qInfo() << name << "was already online at startup - pending auto-connect";
            m_PendingAutoConnect.insert(computer->uuid);
        }
    }
}

int ComputerModel::takePendingAutoConnect()
{
    m_StartupPhase = false;

    int result = -1;
    for (int i = 0; i < m_Computers.count() && result < 0; i++) {
        NvComputer* computer = m_Computers[i];
        if (!m_PendingAutoConnect.contains(computer->uuid)) {
            continue;
        }

        bool online;
        {
            QReadLocker lock(&computer->lock);
            online = computer->state == NvComputer::CS_ONLINE;
        }
        if (online && isEligibleForAutoConnect(computer, nullptr)) {
            result = i;
        }
    }

    m_PendingAutoConnect.clear();
    return result;
}

QString ComputerModel::getComputerName(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    QReadLocker lock(&m_Computers[computerIndex]->lock);
    return m_Computers[computerIndex]->name;
}

bool ComputerModel::isEligibleForAutoConnect(NvComputer* computer, QString* name)
{
    QReadLocker lock(&computer->lock);
    if (name != nullptr) {
        *name = computer->name;
    }
    return computer->pairState == NvComputer::PS_PAIRED &&
           computer->isSupportedServerVersion;
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        return tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[DetailsRole] = "details";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    m_LastKnownStates.remove(m_Computers[computerIndex]->uuid);
    m_PendingAutoConnect.remove(m_Computers[computerIndex]->uuid);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(m_Computers[computerIndex]);
    QThreadPool::globalInstance()->start(wakeTask);
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->pairHost(m_Computers[computerIndex], pin);
}

void ComputerModel::handlePairingCompleted(NvComputer*, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Reset the model if the structural layout of the list has changed
    if (m_Computers != newComputerList) {
        beginResetModel();
        m_Computers = newComputerList;
        endResetModel();
    }
    else {
        // Let the view know that this specific computer changed
        int index = m_Computers.indexOf(computer);
        emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }

    checkForOnlineTransition(computer);
}

void ComputerModel::checkForOnlineTransition(NvComputer* computer)
{
    NvComputer::ComputerState newState;
    {
        QReadLocker lock(&computer->lock);
        newState = computer->state;
    }
    QString name;
    bool eligible = isEligibleForAutoConnect(computer, &name);

    // Fire on any transition into CS_ONLINE, including the initial
    // UNKNOWN -> ONLINE at startup, so a host that is already hosting when
    // Moonlight launches is auto-connected too.
    NvComputer::ComputerState oldState = m_LastKnownStates.value(computer->uuid, NvComputer::CS_UNKNOWN);
    m_LastKnownStates[computer->uuid] = newState;

    if (oldState == NvComputer::CS_ONLINE || newState != NvComputer::CS_ONLINE) {
        return;
    }

    if (!eligible) {
        qInfo() << name << "came online but is not eligible for auto-connect (unpaired or unsupported)";
        return;
    }

    int index = m_Computers.indexOf(computer);
    if (index < 0) {
        return;
    }

    if (m_StartupPhase) {
        // The PC view has not asked for pending hosts yet, so nobody is
        // listening to the signal. Hold on to it until it does.
        qInfo() << name << "came online before the PC view was ready - pending auto-connect";
        m_PendingAutoConnect.insert(computer->uuid);
        return;
    }

    qInfo() << name << "came online - requesting auto-connect";
    emit computerCameOnline(index, name);
}

#include "computermodel.moc"
