// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QVBoxLayout>

#include "lichii/util/keyboard_controller.h"
#include "lichii/util/keyboard_input.h"
#include "lichii/util/keyboard_panel.h"

using Lichii::KeyboardController;
using Lichii::KeyboardInput;
using Lichii::KeyboardLanguage;
using Lichii::KeyboardPanel;

namespace {

Settings::Language AmericanEnglish() {
    return Settings::Language::EnglishAmerican;
}

QRect InWindow(const QWidget& child, const QWidget& window) {
    return {child.mapTo(&window, QPoint{}), child.size()};
}

QPushButton* VisibleKey(KeyboardPanel& panel, const QString& text) {
    const auto keys = panel.findChildren<QPushButton*>();
    for (auto* key : keys) {
        if (key->isVisible() && key->isEnabled() && key->text() == text) {
            return key;
        }
    }
    return nullptr;
}

} // namespace

class KeyboardTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }

    void dictionaries() {
        const QStringList names{QStringLiteral("en-US"), QStringLiteral("en-GB"),
                                QStringLiteral("zh-Hans"), QStringLiteral("zh-Hant"),
                                QStringLiteral("wubi-Hans"), QStringLiteral("wubi-Hant"),
                                QStringLiteral("ja")};
        for (const auto& name : names) {
            QFile resource(QStringLiteral(":/keyboard/dictionaries/%1.bin").arg(name));
            QVERIFY(resource.open(QIODevice::ReadOnly));
            QVERIFY2(resource.size() > 10000, qPrintable(name));
        }
    }

    void phrases_data() {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("keys");
        QTest::addColumn<QString>("candidate");
        auto row = [](const char* name, KeyboardLanguage language, const QString& keys,
                      const QString& candidate) {
            QTest::newRow(name) << static_cast<int>(language) << keys << candidate;
        };
        row("US color", KeyboardLanguage::EnglishUS, QStringLiteral("colo"),
            QStringLiteral("color"));
        row("UK colour", KeyboardLanguage::EnglishUK, QStringLiteral("colo"),
            QStringLiteral("colour"));
        row("simplified hello", KeyboardLanguage::ChineseSimplified, QStringLiteral("nihao"),
            QStringLiteral("你好"));
        row("simplified Chinese", KeyboardLanguage::ChineseSimplified, QStringLiteral("zhongwen"),
            QStringLiteral("中文"));
        row("traditional study", KeyboardLanguage::ChineseTraditional, QStringLiteral("xuexi"),
            QStringLiteral("學習"));
        row("Japanese Japan", KeyboardLanguage::Japanese, QStringLiteral("nihon"),
            QStringLiteral("日本"));
    }

    void phrases() {
        QFETCH(int, language);
        QFETCH(QString, keys);
        QFETCH(QString, candidate);
        KeyboardInput input;
        input.SetLanguage(static_cast<KeyboardLanguage>(language));
        QString text;
        for (QChar key : keys) {
            const QString letter(key);
            text += input.Feed(letter);
        }
        const int index = static_cast<int>(input.Candidates().indexOf(candidate));
        QVERIFY2(index >= 0, qPrintable(input.Candidates().join(QStringLiteral(" | "))));
        text += input.AcceptCandidate(index);
        QCOMPARE(text.trimmed(), candidate);
        QVERIFY(input.Preedit().isEmpty());
    }

    void moveOnlyWhenCovered() {
        KeyboardController keyboard(this, AmericanEnglish);
        QDialog window;
        auto* layout = new QVBoxLayout(&window);
        auto* top = new QLineEdit(&window);
        auto* bottom = new QLineEdit(&window);
        layout->addWidget(top);
        layout->addStretch(1);
        layout->addWidget(bottom);
        window.resize(760, 630);
        window.show();
        top->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const QRect baseline_top = InWindow(*top, window);
        const QRect baseline_bottom = InWindow(*bottom, window);
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->isVisible() && panel->geometry().bottom() == window.height() - 1);
        QCOMPARE(InWindow(*top, window), baseline_top);
        keyboard.Dismiss();
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(InWindow(*top, window), baseline_top);
        QCOMPARE(InWindow(*bottom, window), baseline_bottom);

        bottom->setFocus();
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->isVisible() && panel->geometry().bottom() == window.height() - 1);
        QVERIFY(InWindow(*bottom, window).bottom() < panel->geometry().top());
        keyboard.HandleControllerKey(&window, Qt::Key_F10);
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(InWindow(*bottom, window), baseline_bottom);
    }

    void nativeInputDialogRestoresSize() {
        KeyboardController keyboard(this, AmericanEnglish);
        QInputDialog window;
        window.setLabelText(QStringLiteral("Profile name"));
        window.setTextValue(QStringLiteral("Player"));
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* editor = window.findChild<QLineEdit*>();
        QVERIFY(editor);
        editor->setFocus();
        const auto original_geometry = window.geometry();
        const auto original_maximum = window.maximumSize();
        const auto original_constraint = window.layout()->sizeConstraint();

        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->isVisible() && panel->geometry().bottom() == window.height() - 1);
        QVERIFY(panel->height() >= panel->minimumSizeHint().height());
        QVERIFY(InWindow(*editor, window).bottom() < panel->geometry().top());

        keyboard.Dismiss();
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(window.geometry(), original_geometry);
        QCOMPARE(window.maximumSize(), original_maximum);
        QCOMPARE(window.layout()->sizeConstraint(), original_constraint);
    }

    void mouseAndControllerInput() {
        KeyboardController keyboard(this, AmericanEnglish);
        QDialog window;
        auto* layout = new QVBoxLayout(&window);
        auto* text = new QLineEdit(&window);
        text->setText(QStringLiteral("replace"));
        layout->addWidget(text);
        layout->addStretch(1);
        window.resize(760, 550);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::mouseClick(text, Qt::LeftButton);
        QTRY_VERIFY(keyboard.IsOpenFor(&window));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        text->selectAll();
        auto* key = VisibleKey(*panel, QStringLiteral("q"));
        QVERIFY(key);
        QTest::mouseClick(key, Qt::LeftButton);
        QCOMPARE(text->text(), QStringLiteral("q"));
        QCOMPARE(QApplication::focusWidget(), text);
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Menu));
        QCOMPARE(text->text(), QString{});
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());
    }

    void followsSystemLanguage() {
        auto system_language = Settings::Language::Chinese;
        KeyboardController keyboard(this, [&] { return system_language; });
        QDialog window;
        auto* layout = new QVBoxLayout(&window);
        auto* text = new QLineEdit(&window);
        layout->addStretch();
        layout->addWidget(text);
        window.resize(820, 540);
        window.show();
        text->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        QVERIFY(!VisibleKey(*panel, QStringLiteral("简体")));
        QVERIFY(!VisibleKey(*panel, QStringLiteral("繁體")));
        QVERIFY(!VisibleKey(*panel, QStringLiteral("EN US")));
        const auto type_candidate = [&](const QString& keys, const QString& word,
                                        bool committed_prefix = false) {
            for (QChar key : keys) {
                auto* button = VisibleKey(*panel, QString(key));
                QVERIFY(button);
                QTest::mouseClick(button, Qt::LeftButton);
            }
            QCOMPARE(text->text(), committed_prefix ? keys : QString{});
            auto* candidate = VisibleKey(*panel, word);
            QVERIFY(candidate);
            QTest::mouseClick(candidate, Qt::LeftButton);
            QCOMPARE(text->text(), word);
        };
        type_candidate(QStringLiteral("nihao"), QStringLiteral("你好"));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());

        text->clear();
        system_language = Settings::Language::ChineseTraditional;
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        type_candidate(QStringLiteral("xuexi"), QStringLiteral("學習"));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());

        text->clear();
        system_language = Settings::Language::Japanese;
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        QVERIFY(!panel->findChild<QPushButton*>(QStringLiteral("keyboardInputModeSwitch"))
                     ->isVisible());
        type_candidate(QStringLiteral("nihon"), QStringLiteral("日本"));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());

        text->clear();
        system_language = Settings::Language::EnglishBritish;
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        type_candidate(QStringLiteral("colo"), QStringLiteral("colour"), true);
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());

        text->clear();
        system_language = Settings::Language::EnglishAmerican;
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        type_candidate(QStringLiteral("colo"), QStringLiteral("color"), true);
    }

    void localizedNumber() {
        KeyboardController keyboard(this, AmericanEnglish);
        QDialog window;
        auto* layout = new QVBoxLayout(&window);
        auto* value = new QDoubleSpinBox(&window);
        value->setLocale(QLocale(QLocale::German, QLocale::Germany));
        value->setRange(-10, 10);
        value->setValue(0);
        layout->addWidget(value);
        layout->addStretch(1);
        window.resize(640, 500);
        window.show();
        value->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        QVERIFY(VisibleKey(*panel, QStringLiteral(",")));
        value->selectAll();
        panel->TextTyped(QStringLiteral("-1,5"));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_F10));
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(value->value(), -1.5);
    }

    void switchChineseInputModes_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("narrow") << 320;
        QTest::newRow("standard") << 660;
    }

    void switchChineseInputModes() {
        QFETCH(int, width);
        auto system_language = Settings::Language::ChineseSimplified;
        KeyboardController keyboard(this, [&] { return system_language; });
        QDialog window;
        auto* layout = new QVBoxLayout(&window);
        auto* text = new QLineEdit(&window);
        layout->addStretch();
        layout->addWidget(text);
        window.resize(width, 540);
        window.show();
        text->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        auto* panel = window.findChild<KeyboardPanel*>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        auto* mode = panel->findChild<QPushButton*>(QStringLiteral("keyboardInputModeSwitch"));
        QVERIFY(mode && mode->isVisible());
        QVERIFY(mode->text().startsWith(QStringLiteral("简体拼音")));
        auto* hide = panel->findChild<QPushButton*>(QStringLiteral("keyboardDismiss"));
        QVERIFY(hide && hide->isVisible());
        QVERIFY(mode->geometry().right() < hide->geometry().left());
        QVERIFY(mode->fontMetrics().horizontalAdvance(mode->text()) < mode->width());
        QCOMPARE(panel->width(), window.width());
        const auto panel_geometry = panel->geometry();
        const auto editor_geometry = InWindow(*text, window);
        QVERIFY(editor_geometry.bottom() < panel_geometry.top());

        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Up));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Up));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Up));
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Left));
        QVERIFY(mode->property("oskFocus").toBool());
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QVERIFY(mode->text().startsWith(QStringLiteral("ABC")));
        QCOMPARE(QApplication::focusWidget(), text);
        QCOMPARE(panel->geometry(), panel_geometry);
        QCOMPARE(InWindow(*text, window), editor_geometry);

        const auto type_keys = [&](const QString& keys) {
            for (QChar key : keys) {
                auto* button = VisibleKey(*panel, QString(key));
                QVERIFY(button);
                QTest::mouseClick(button, Qt::LeftButton);
            }
        };
        type_keys(QStringLiteral("nihao"));
        QCOMPARE(text->text(), QStringLiteral("nihao"));
        QVERIFY(!VisibleKey(*panel, QStringLiteral("你好")));

        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(mode->text().startsWith(QStringLiteral("五笔 86")));
        text->clear();
        type_keys(QStringLiteral("wqvb"));
        QCOMPARE(text->text(), QString{});
        auto* candidate = VisibleKey(*panel, QStringLiteral("你好"));
        QVERIFY(candidate);
        QTest::mouseClick(candidate, Qt::LeftButton);
        QCOMPARE(text->text(), QStringLiteral("你好"));

        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Escape));
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        QVERIFY(mode->text().startsWith(QStringLiteral("五笔 86")));
        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(mode->text().startsWith(QStringLiteral("简体拼音")));

        text->clear();
        type_keys(QStringLiteral("nihao"));
        QCOMPARE(text->text(), QString{});
        QTest::mouseClick(mode, Qt::LeftButton);
        QCOMPARE(text->text(), QStringLiteral("你好"));

        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Escape));
        QTRY_VERIFY(!panel->isVisible());
        system_language = Settings::Language::ChineseTraditional;
        QVERIFY(keyboard.HandleControllerKey(&window, Qt::Key_Enter));
        QTRY_VERIFY(panel->geometry().bottom() == window.height() - 1);
        QVERIFY(mode->text().startsWith(QStringLiteral("繁體拼音")));
        QTest::mouseClick(mode, Qt::LeftButton);
        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(mode->text().startsWith(QStringLiteral("五筆 86")));
        text->clear();
        type_keys(QStringLiteral("ipnu"));
        candidate = VisibleKey(*panel, QStringLiteral("學習"));
        QVERIFY(candidate);
        QTest::mouseClick(candidate, Qt::LeftButton);
        QCOMPARE(text->text(), QStringLiteral("學習"));
    }
};

QTEST_MAIN(KeyboardTest)
#include "keyboard.moc"
