#ifndef NFCSETTINGS_H
#define NFCSETTINGS_H

#include <systemsettingsglobal.h>

#include <QObject>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QTimer>

class SYSTEMSETTINGS_EXPORT NfcSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit NfcSettings(QObject *parent = nullptr);
    ~NfcSettings();

    bool available() const;
    bool enabled() const;
    void setEnabled(bool enabled);

signals:
    void availableChanged();
    void enabledChanged();

private:
    bool m_enabled;
    bool m_available;
    QDBusInterface *m_interface;
    QTimer *m_timer;

private slots:
    void getEnableStateFinished(QDBusPendingCallWatcher* call);
    void updateEnabledState(bool enabled);
};

#endif // NFCSETTINGS_H
