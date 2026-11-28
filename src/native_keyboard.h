#ifndef KOBROWSE_NATIVE_KEYBOARD_H
#define KOBROWSE_NATIVE_KEYBOARD_H

// Adapted from Kobalt Qt 5 NativeKeyboard, revision 4e1bd56743b20751e2429ec97cc14af169c1af70.
// Copyright 2026 Nico Verbruggen; MIT license in res/KOBALT-LICENSE.txt.
#include <QObject>
#include <QPointer>
#include <QLineEdit>
#include <QWidget>
#include <QLocale>
#include <functional>

enum KeyboardScript : int;

class NativeKeyboard : public QObject {
    Q_OBJECT
public:
    struct Api {
        void (*frame)(void *, QWidget *);
        void (*destroyFrame)(QWidget *);
        QObject *(*create)(QWidget *, int, const QLocale &);
        void (*receiver)(void *, QLineEdit *, bool);
        void (*attach)(QObject *, QObject *, bool);
        void (*goText)(QObject *, const QString &);
        Api();
        bool valid() const;
    };
    NativeKeyboard(QWidget *host, QLineEdit *editor);
    // Test-only injection: caller supplies fake objects, never real Nickel on desktop.
    NativeKeyboard(QWidget *host, QLineEdit *editor, const Api &api, size_t frameBytes, size_t receiverBytes);
    ~NativeKeyboard() override;
    bool showKeyboard();
    void hideKeyboard();
    void place();
    bool owns(QWidget *widget) const;
    std::function<void()> submitted;
    std::function<void(int)> heightChanged;
private slots:
    void commit();
    void changeScript(KeyboardScript script);
private:
    bool create();
    void reset();
    QPointer<QWidget> frame_;
    QPointer<QLineEdit> editor_;
    QPointer<QObject> receiver_;
    QWidget *host_;
    Api api_;
    size_t frameBytes_, receiverBytes_;
    bool injected_, ready_;
    int script_;
};
#endif
