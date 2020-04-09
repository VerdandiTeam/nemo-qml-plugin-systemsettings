/*
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include "usermodel.h"
#include "logging_p.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QString>
#include <functional>
#include <grp.h>
#include <sailfishaccesscontrol.h>
#include <sailfishusermanagerinterface.h>
#include <sys/types.h>

namespace {
const auto UserManagerService = QStringLiteral(SAILFISH_USERMANAGER_DBUS_INTERFACE);
const auto UserManagerPath = QStringLiteral(SAILFISH_USERMANAGER_DBUS_OBJECT_PATH);
const auto UserManagerInterface = QStringLiteral(SAILFISH_USERMANAGER_DBUS_INTERFACE);

const QHash<const QString, int> errorTypeMap = {
    { QStringLiteral(SailfishUserManagerErrorBusy), UserModel::Busy },
    { QStringLiteral(SailfishUserManagerErrorHomeCreateFailed), UserModel::HomeCreateFailed },
    { QStringLiteral(SailfishUserManagerErrorHomeRemoveFailed), UserModel::HomeRemoveFailed },
    { QStringLiteral(SailfishUserManagerErrorGroupCreateFailed), UserModel::GroupCreateFailed },
    { QStringLiteral(SailfishUserManagerErrorUserAddFailed), UserModel::UserAddFailed },
    { QStringLiteral(SailfishUserManagerErrorUserModifyFailed), UserModel::UserModifyFailed },
    { QStringLiteral(SailfishUserManagerErrorUserRemoveFailed), UserModel::UserRemoveFailed },
    { QStringLiteral(SailfishUserManagerErrorGetUidFailed), UserModel::GetUidFailed },
    { QStringLiteral(SailfishUserManagerErrorUserNotFound), UserModel::UserNotFound },
    { QStringLiteral(SailfishUserManagerErrorAddToGroupFailed), UserModel::AddToGroupFailed },
    { QStringLiteral(SailfishUserManagerErrorRemoveFromGroupFailed), UserModel::RemoveFromGroupFailed },
};

int getErrorType(QDBusError &error)
{
    if (error.type() != QDBusError::Other)
        return error.type();

    return errorTypeMap.value(error.name(), UserModel::OtherError);
}
}

UserModel::UserModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_dBusInterface(nullptr)
    , m_dBusWatcher(new QDBusServiceWatcher(UserManagerService, QDBusConnection::systemBus(),
                    QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this))
{
    qDBusRegisterMetaType<SailfishUserManagerEntry>();
    connect(m_dBusWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &UserModel::createInterface);
    connect(m_dBusWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &UserModel::destroyInterface);
    if (QDBusConnection::systemBus().interface()->isServiceRegistered(UserManagerService))
        createInterface();
    struct group *grp = getgrnam("users");
    for (int i = 0; grp->gr_mem[i] != nullptr; ++i) {
        UserInfo user(QString(grp->gr_mem[i]));
        if (user.isValid()) { // Skip invalid users here
            m_users.append(user);
            m_uidsToRows.insert(user.uid(), m_users.count()-1);
        }
    }
    // grp must not be free'd
}

UserModel::~UserModel()
{
}

bool UserModel::placeholder()
{
    // Placeholder is always last and the only item that can be invalid
    if (m_users.count() == 0)
        return false;
    return !m_users.last().isValid();
}

void UserModel::setPlaceholder(bool value)
{
    if (placeholder() == value)
        return;

    if (value) {
        int row = m_users.count();
        beginInsertRows(QModelIndex(), row, row);
        m_users.append(UserInfo::placeholder());
        endInsertRows();
    } else {
        int row = m_users.count()-1;
        beginRemoveRows(QModelIndex(), row, row);
        m_users.remove(row);
        endRemoveRows();
    }
    emit placeholderChanged();
}

QHash<int, QByteArray> UserModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { Qt::DisplayRole, "displayName" },
        { UsernameRole, "username" },
        { NameRole, "name" },
        { TypeRole, "type" },
        { UidRole, "uid" },
        { CurrentRole, "current" },
        { PlaceholderRole, "placeholder" },
    };
    return roles;
}

int UserModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_users.count();
}

QVariant UserModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_users.count() || index.column() != 0)
        return QVariant();

    const UserInfo &user = m_users.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return user.displayName();
    case UsernameRole:
        return user.username();
    case NameRole:
        return user.name();
    case TypeRole:
        return user.type();
    case UidRole:
        return user.uid();
    case CurrentRole:
        return user.current();
    case PlaceholderRole:
        return !user.isValid();
    default:
        return QVariant();
    }
}

bool UserModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.row() < 0 || index.row() >= m_users.count() || index.column() != 0)
        return false;

    UserInfo &user = m_users[index.row()];
    switch (role) {
    case NameRole: {
        QString name = value.toString();
        if (name.isEmpty() || name == user.name())
            return false;
        user.setName(name);
        if (user.isValid()) {
            createInterface();
            auto call = m_dBusInterface->asyncCall(QStringLiteral("modifyUser"), (uint)user.uid(), name);
            auto *watcher = new QDBusPendingCallWatcher(call, this);
            connect(watcher, &QDBusPendingCallWatcher::finished,
                    this, std::bind(&UserModel::userModifyFinished, this, std::placeholders::_1, index.row()));
        }
        emit dataChanged(index, index, QVector<int>() << role);
        return true;
    }
    case Qt::DisplayRole:
    case UsernameRole:
    case TypeRole:
    case UidRole:
    case CurrentRole:
    case PlaceholderRole:
    default:
        return false;
    }
}

QModelIndex UserModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    if (row < 0 || row >= m_users.count() || column != 0)
        return QModelIndex();

    // create index
    return createIndex(row, 0, row);
}

/*
 * Creates new user from a placeholder user.
 *
 * Does nothing if there is no placeholder or user's name is not set.
 */
void UserModel::createUser()
{
    if (!placeholder())
        return;

    auto user = m_users.last();
    if (user.name().isEmpty())
        return;

    createInterface();
    auto call = m_dBusInterface->asyncCall(QStringLiteral("addUser"), user.name());
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &UserModel::userAddFinished);
}

void UserModel::removeUser(int row)
{
    if (row < 0 || row >= m_users.count())
        return;

    auto user = m_users.at(row);
    if (!user.isValid())
        return;

    createInterface();
    auto call = m_dBusInterface->asyncCall(QStringLiteral("removeUser"), (uint)user.uid());
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, std::bind(&UserModel::userRemoveFinished, this, std::placeholders::_1, row));
}

void UserModel::setCurrentUser(int row)
{
    if (row < 0 || row >= m_users.count())
        return;

    auto user = m_users.at(row);
    if (!user.isValid())
        return;

    createInterface();
    auto call = m_dBusInterface->asyncCall(QStringLiteral("setCurrentUser"), (uint)user.uid());
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, std::bind(&UserModel::setCurrentUserFinished, this, std::placeholders::_1, row));
}

void UserModel::reset(int row)
{
    if (row < 0 || row >= m_users.count())
        return;

    m_users[row].reset();
    auto idx = index(row, 0);
    emit dataChanged(idx, idx, QVector<int>());
}

UserInfo * UserModel::getCurrentUser() const
{
    return new UserInfo();
}

bool UserModel::hasGroup(int row, const QString &group) const
{
    if (row < 0 || row >= m_users.count())
        return false;

    auto user = m_users.at(row);
    if (!user.isValid())
        return false;

    return sailfish_access_control_hasgroup(user.uid(), group.toUtf8().constData());
}

void UserModel::addGroups(int row, const QStringList &groups)
{
    if (row < 0 || row >= m_users.count())
        return;

    auto user = m_users.at(row);
    if (!user.isValid())
        return;

    createInterface();
    auto call = m_dBusInterface->asyncCall(QStringLiteral("addToGroups"), (uint)user.uid(), groups);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, std::bind(&UserModel::addToGroupsFinished, this, std::placeholders::_1, row));
}

void UserModel::removeGroups(int row, const QStringList &groups)
{
    if (row < 0 || row >= m_users.count())
        return;

    auto user = m_users.at(row);
    if (!user.isValid())
        return;

    createInterface();
    auto call = m_dBusInterface->asyncCall(QStringLiteral("removeFromGroups"), (uint)user.uid(), groups);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, std::bind(&UserModel::removeFromGroupsFinished, this, std::placeholders::_1, row));
}

void UserModel::onUserAdded(const SailfishUserManagerEntry &entry)
{
    if (m_uidsToRows.contains(entry.uid))
        return;

    // Not found already, appending
    auto user = UserInfo(entry.uid);
    if (user.isValid()) {
        int row = placeholder() ? m_users.count()-1 : m_users.count();
        beginInsertRows(QModelIndex(), row, row);
        m_users.insert(row, user);
        m_uidsToRows.insert(entry.uid, row);
        endInsertRows();
    }
}

void UserModel::onUserModified(uint uid, const QString &newName)
{
    if (!m_uidsToRows.contains(uid))
        return;

    int row = m_uidsToRows.value(uid);
    UserInfo &user = m_users[row];
    if (user.name() != newName) {
        user.setName(newName);
        auto idx = index(row, 0);
        dataChanged(idx, idx, QVector<int>() << NameRole);
    }
}

void UserModel::onUserRemoved(uint uid)
{
    if (!m_uidsToRows.contains(uid))
        return;

    int row = m_uidsToRows.value(uid);
    beginRemoveRows(QModelIndex(), row, row);
    m_users.remove(row);
    // It is slightly costly to remove users since some row numbers may need to be updated
    m_uidsToRows.remove(uid);
    for (auto iter = m_uidsToRows.begin(); iter != m_uidsToRows.end(); ++iter) {
        if (iter.value() > row)
            iter.value() -= 1;
    }
    endRemoveRows();
}

void UserModel::onCurrentUserChanged(uint uid)
{
    UserInfo *previous = getCurrentUser();
    if (previous) {
        if (previous->updateCurrent()) {
            auto idx = index(m_uidsToRows.value(previous->uid()), 0);
            emit dataChanged(idx, idx, QVector<int>() << CurrentRole);
        }
        delete previous;
    }
    if (m_uidsToRows.contains(uid) && m_users[m_uidsToRows.value(uid)].updateCurrent()) {
        auto idx = index(m_uidsToRows.value(uid), 0);
        emit dataChanged(idx, idx, QVector<int>() << CurrentRole);
    }
}

void UserModel::onCurrentUserChangeFailed(uint uid)
{
    if (m_uidsToRows.contains(uid)) {
        int row = m_uidsToRows.value(uid);
        emit setCurrentUserFailed(row, Failure);
    }
}

void UserModel::userAddFinished(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<uint> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit userAddFailed(getErrorType(error));
        qCWarning(lcUsersLog) << "Adding user with usermanager failed:" << error;
    } else {
        uint uid = reply.value();
        // Check that this was not just added to the list by onUserAdded
        if (!m_uidsToRows.contains(uid)) {
            // Add to the end
            int row = m_users.count()-1;
            beginInsertRows(QModelIndex(), row, row);
            m_users.insert(row, UserInfo(uid));
            m_uidsToRows.insert(uid, row);
            endInsertRows();
        }
        // Reset placeholder
        reset(m_users.count()-1);
    }
    call->deleteLater();
}

void UserModel::userModifyFinished(QDBusPendingCallWatcher *call, int row)
{
    QDBusPendingReply<void> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit userModifyFailed(row, getErrorType(error));
        qCWarning(lcUsersLog) << "Modifying user with usermanager failed:" << error;
        reset(row);
    } // else awesome! (data was changed already)
    call->deleteLater();
}

void UserModel::userRemoveFinished(QDBusPendingCallWatcher *call, int row)
{
    QDBusPendingReply<void> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit userRemoveFailed(row, getErrorType(error));
        qCWarning(lcUsersLog) << "Removing user with usermanager failed:" << error;
    } // else awesome! (waiting for signal to alter data)
    call->deleteLater();
}

void UserModel::setCurrentUserFinished(QDBusPendingCallWatcher *call, int row)
{
    QDBusPendingReply<void> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit setCurrentUserFailed(row, getErrorType(error));
        qCWarning(lcUsersLog) << "Switching user with usermanager failed:" << error;
    } // else user switching was initiated successfully
    call->deleteLater();
}

void UserModel::addToGroupsFinished(QDBusPendingCallWatcher *call, int row)
{
    QDBusPendingReply<void> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit addGroupsFailed(row, getErrorType(error));
        qCWarning(lcUsersLog) << "Adding user to groups failed:" << error;
    } else {
        emit userGroupsChanged(row);
    }
    call->deleteLater();
}

void UserModel::removeFromGroupsFinished(QDBusPendingCallWatcher *call, int row)
{
    QDBusPendingReply<void> reply = *call;
    if (reply.isError()) {
        auto error = reply.error();
        emit removeGroupsFailed(row, getErrorType(error));
        qCWarning(lcUsersLog) << "Adding user to groups failed:" << error;
    } else {
        emit userGroupsChanged(row);
    }
    call->deleteLater();
}

void UserModel::createInterface()
{
    if (!m_dBusInterface) {
        qCDebug(lcUsersLog) << "Creating interface to user-managerd";
        m_dBusInterface = new QDBusInterface(UserManagerService, UserManagerPath, UserManagerInterface,
                                             QDBusConnection::systemBus(), this);
        connect(m_dBusInterface, SIGNAL(userAdded(const SailfishUserManagerEntry &)),
                this, SLOT(onUserAdded(const SailfishUserManagerEntry &)), Qt::QueuedConnection);
        connect(m_dBusInterface, SIGNAL(userModified(uint, const QString &)),
                this, SLOT(onUserModified(uint, const QString &)));
        connect(m_dBusInterface, SIGNAL(userRemoved(uint)),
                this, SLOT(onUserRemoved(uint)));
        connect(m_dBusInterface, SIGNAL(currentUserChanged(uint)),
                this, SLOT(onCurrentUserChanged(uint)));
        connect(m_dBusInterface, SIGNAL(currentUserChangeFailed(uint)),
                this, SLOT(onCurrentUserChangeFailed(uint)));
    }
}

void UserModel::destroyInterface() {
    if (m_dBusInterface) {
        qCDebug(lcUsersLog) << "Destroying interface to user-managerd";
        disconnect(m_dBusInterface, 0, this, 0);
        m_dBusInterface->deleteLater();
        m_dBusInterface = nullptr;
    }
}