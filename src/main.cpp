#include "calculator.h"
#include "converter.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QUrl>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFile>
#include <QGridLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QPainter>
#include <QPalette>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>

using jatayu::Calculator;

namespace {

constexpr int panelWidth = 335;
constexpr int historyLimit = 100;

QString asText(const std::string &value)
{
    return QString::fromStdString(value);
}

class Readout : public QLabel {
public:
    explicit Readout(int baseSize, int minimumSize)
        : baseSize(baseSize), minimumSize(minimumSize)
    {
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    }

    void showValue(const QString &value)
    {
        fullText = value;
        fit();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        fit();
    }

private:
    int baseSize;
    int minimumSize;
    QString fullText;

    void fit()
    {
        auto scaled = font();
        int size = baseSize;
        scaled.setPixelSize(size);
        while (size > minimumSize && QFontMetrics(scaled).horizontalAdvance(fullText) > width())
            scaled.setPixelSize(--size);
        setFont(scaled);
        setText(QFontMetrics(scaled).elidedText(fullText, Qt::ElideLeft, width()));
        setAccessibleName(fullText);
        setToolTip(text() == fullText ? QString() : "<span>" + fullText.toHtmlEscaped() + "</span>");
    }
};

class HistorySwitch : public QPushButton {
public:
    HistorySwitch()
    {
        setCheckable(true);
    }

    QSize sizeHint() const override
    {
        return QSize(fontMetrics().horizontalAdvance(text()) + 55, 34);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF track(6, (height() - 18) / 2.0, 35, 18);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? QColor("#3f844c") : QColor("#47544f"));
        painter.drawRoundedRect(track, 9, 9);
        painter.setBrush(QColor("#f4f7f5"));
        painter.drawEllipse(QRectF(track.left() + (isChecked() ? 19 : 3), track.top() + 2, 14, 14));
        painter.setPen(QColor("#f4f7f5"));
        painter.drawText(rect().adjusted(49, 0, 0, 0), Qt::AlignVCenter, text());
        if (hasFocus()) {
            painter.setPen(QColor("#82ad8c"));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 5, 5);
        }
    }
};

class ConverterPanel : public QWidget {
public:
    ConverterPanel()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 13, 12, 12);
        layout->setSpacing(9);
        category = new QComboBox;
        category->setObjectName("conversionCategory");
        layout->addWidget(categoryLabel = new QLabel);
        layout->addWidget(category);
        input = new QLineEdit("1");
        input->setObjectName("conversionInput");
        input->setMaxLength(45);
        input->setClearButtonEnabled(true);
        layout->addWidget(valueLabel = new QLabel);
        layout->addWidget(input);

        auto *units = new QGridLayout;
        units->setHorizontalSpacing(8);
        units->addWidget(fromLabel = new QLabel, 0, 0);
        units->addWidget(toLabel = new QLabel, 0, 2);
        from = new QComboBox;
        from->setObjectName("fromUnit");
        to = new QComboBox;
        to->setObjectName("toUnit");
        for (auto *selection : {from, to}) {
            selection->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            selection->setMinimumContentsLength(7);
            selection->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        }
        swap = new QPushButton("⇄");
        swap->setObjectName("swapUnits");
        units->addWidget(from, 1, 0);
        units->addWidget(swap, 1, 1);
        units->addWidget(to, 1, 2);
        units->setColumnStretch(0, 1);
        units->setColumnStretch(2, 1);
        layout->addLayout(units);

        auto *card = new QWidget;
        card->setObjectName("display");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->addWidget(summary = new Readout(16, 12));
        summary->setObjectName("expression");
        summary->setMinimumHeight(24);
        cardLayout->addWidget(output = new Readout(48, 16));
        output->setObjectName("conversionResult");
        output->setMinimumHeight(58);
        layout->addWidget(card);
        auto *actions = new QHBoxLayout;
        actions->addStretch();
        actions->addWidget(copyButton = new QPushButton);
        layout->addLayout(actions);
        layout->addStretch();

        categoryLabel->setBuddy(category);
        valueLabel->setBuddy(input);
        fromLabel->setBuddy(from);
        toLabel->setBuddy(to);
        connect(category, &QComboBox::currentIndexChanged, this, [this] { populateUnits(0, 1); });
        connect(from, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(to, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(input, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(swap, &QPushButton::clicked, this, [this] {
            const auto converted = answer;
            {
                const QSignalBlocker sourceBlock(from), targetBlock(to), inputBlock(input);
                const int previousSource = from->currentIndex();
                from->setCurrentIndex(to->currentIndex());
                to->setCurrentIndex(previousSource);
                if (!converted.isEmpty())
                    input->setText(converted);
            }
            refresh();
        });
        connect(copyButton, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(answer);
        });
    }

    void translate(bool useHindi)
    {
        hindi = useHindi;
        categoryLabel->setText(localized("Category", "श्रेणी"));
        valueLabel->setText(localized("Value", "मान"));
        fromLabel->setText(localized("From", "से"));
        toLabel->setText(localized("To", "में"));
        swap->setToolTip(localized("Swap units", "इकाइयाँ बदलें"));
        swap->setAccessibleName(swap->toolTip());
        copyButton->setText(localized("Copy result", "परिणाम कॉपी करें"));
        const int previousCategory = std::max(0, category->currentIndex());
        {
            const QSignalBlocker blocker(category);
            category->clear();
            for (const auto &group : jatayu::conversionCategories())
                category->addItem(localized(group.english, group.hindi));
            category->setCurrentIndex(previousCategory);
        }
        populateUnits(std::max(0, from->currentIndex()), to->currentIndex() < 0 ? 1 : to->currentIndex());
    }

private:
    QComboBox *category;
    QComboBox *from;
    QComboBox *to;
    QLabel *categoryLabel;
    QLabel *valueLabel;
    QLabel *fromLabel;
    QLabel *toLabel;
    QLineEdit *input;
    QPushButton *swap;
    QPushButton *copyButton;
    Readout *summary;
    Readout *output;
    QString answer;
    bool hindi = false;

    QString localized(const char *english, const char *translation) const
    {
        return QString::fromUtf8(hindi ? translation : english);
    }

    const std::vector<jatayu::Unit> &selectedUnits() const
    {
        return jatayu::conversionCategories().at(category->currentIndex()).units;
    }

    void populateUnits(int source, int target)
    {
        {
            const QSignalBlocker sourceBlock(from), targetBlock(to);
            from->clear();
            to->clear();
            for (const auto &unit : selectedUnits()) {
                const QString label = localized(unit.english, unit.hindi) + " (" + QString::fromUtf8(unit.symbol) + ")";
                from->addItem(label);
                to->addItem(label);
            }
            from->setCurrentIndex(source);
            to->setCurrentIndex(target);
        }
        refresh();
    }

    void refresh()
    {
        const auto &source = selectedUnits().at(from->currentIndex());
        const auto &target = selectedUnits().at(to->currentIndex());
        const auto symbol = QString::fromUtf8(target.symbol);
        auto number = input->text().trimmed();
        for (qsizetype i = 0; i < number.size(); ++i) {
            const auto digit = number.at(i).digitValue();
            if (digit >= 0)
                number[i] = QChar('0' + digit);
            else if (number.at(i) == QChar(0x2212))
                number[i] = '-';
        }
        static const QRegularExpression numeric(R"(\A[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]{1,3})?\z)");
        answer.clear();
        QString message;
        if (numeric.match(number).hasMatch()) {
            try {
                auto ascii = number.toStdString();
                if (ascii.front() == '+')
                    ascii.erase(0, 1);
                const auto value = jatayu::convert(jatayu::Decimal(ascii), source, target);
                answer = asText(jatayu::numberText(value));
            } catch (jatayu::Error) {
                message = localized("Value out of range", "मान सीमा से बाहर है");
            } catch (const std::exception &) {
                message = localized("Enter a number", "संख्या दर्ज करें");
            }
        } else if (!number.isEmpty()) {
            message = localized("Enter a number", "संख्या दर्ज करें");
        }
        const auto description = number + " " + QString::fromUtf8(source.symbol) + " → " + symbol;
        summary->showValue(!message.isEmpty() ? message : answer.isEmpty() ? symbol : description);
        output->showValue(answer.isEmpty() ? "—" : asText(jatayu::grouped(answer.toStdString())) + " " + symbol);
        copyButton->setEnabled(!answer.isEmpty());
    }
};

class CalculatorWindow : public QWidget {
public:
    CalculatorWindow()
    {
        setWindowIcon(QIcon::fromTheme("jatayu-calculator"));
        setObjectName("window");
        setMinimumHeight(485);
        setFocusPolicy(Qt::StrongFocus);
        hindi = settings.value("hindi", false).toBool();
        const auto savedHistory = settings.value("history").toList();
        for (qsizetype index = 0; index < std::min<qsizetype>(historyLimit, savedHistory.size()); ++index) {
            const auto record = savedHistory.at(index).toMap();
            const auto savedExpression = record.value("expression").toString();
            const auto savedResult = record.value("result").toString();
            const auto savedSnapshot = record.value("snapshot").toString();
            static const QRegularExpression resultFormat(R"(\A-?[0-9][0-9,.]*(?:[eE][+-]?[0-9]+)?\z)");
            if (!savedExpression.isEmpty() && savedExpression.size() <= qsizetype(Calculator::maxExpressionBytes)
                && savedResult.size() <= 96 && resultFormat.match(savedResult).hasMatch()
                && savedSnapshot.size() <= qsizetype(Calculator::maxSnapshotBytes)
                && savedSnapshot.toUtf8().size() <= qsizetype(Calculator::maxSnapshotBytes)) {
                history.append(QVariantMap{{"expression", savedExpression}, {"result", savedResult},
                    {"snapshot", savedSnapshot}, {"mode", record.value("mode").toInt()},
                    {"radians", record.value("radians").toBool()}});
            }
        }

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *toolbar = new QWidget;
        toolbar->setObjectName("toolbar");
        auto *tools = new QHBoxLayout(toolbar);
        tools->setContentsMargins(13, 8, 13, 8);
        tools->setSpacing(12);

        mode = new QComboBox;
        mode->setMinimumWidth(150);
        mode->setFocusPolicy(Qt::TabFocus);
        mode->setObjectName("mode");
        mode->addItems({QString(), QString(), QString()});
        tools->addWidget(mode);

        language = new QComboBox;
        language->setMinimumWidth(73);
        language->setFocusPolicy(Qt::TabFocus);
        language->addItem("EN");
        language->addItem("हिन्दी");
        language->setCurrentIndex(hindi ? 1 : 0);
        tools->addWidget(language);

        auto *separator = new QFrame;
        separator->setObjectName("toolbarSeparator");
        separator->setFixedWidth(1);
        tools->addWidget(separator);
        historyToggle = new HistorySwitch;
        tools->addWidget(historyToggle);
        tools->addStretch();
        helpMenuButton = new QToolButton;
        helpMenuButton->setObjectName("helpMenuButton");
        helpMenuButton->setPopupMode(QToolButton::InstantPopup);
        auto *helpMenu = new QMenu(helpMenuButton);
        helpAction = helpMenu->addAction(QString(), this, [this] { showHelp(); });
        helpAction->setShortcut(QKeySequence(Qt::Key_F1));
        addAction(helpAction);
        aboutAction = helpMenu->addAction(QString(), this, [this] {
            showInformation(aboutAction->text(),
                windowTitle() + "\n" + localized("Version ", "संस्करण ") + QApplication::applicationVersion()
                + "\n\n" + localized("Created by Anup Kumar Yadav", "निर्माता अनुप कुमार यादव")
                + "\n" + localized("A desktop calculator for JatayuOS.", "जटायु ओएस के लिए डेस्कटॉप कैलकुलेटर।"), true);
        });
        creditsAction = helpMenu->addAction(QString(), this, [this] {
            QFile license(QStringLiteral(":/licenses/LICENSE"));
            const QString licenseText = license.open(QIODevice::ReadOnly)
                ? QString::fromUtf8(license.readAll()) : QString();
            showInformation(localized("License & Credits", "लाइसेंस और आभार"), localized(
                "Jatayu Calculator\nMIT License · Copyright (c) 2026 Anup Kumar Yadav\n\n"
                "Credits\nQt 6 is provided by the system under LGPL-3.0. "
                "Boost.Multiprecision provides decimal arithmetic under the Boost Software License 1.0.",
                "जटायु कैलकुलेटर\nMIT लाइसेंस · कॉपीराइट (c) 2026 अनुप कुमार यादव\n\n"
                "आभार\nQt 6 सिस्टम से LGPL-3.0 लाइसेंस के तहत मिलता है। "
                "दशमलव गणना के लिए Boost.Multiprecision का उपयोग Boost Software License 1.0 के तहत किया गया है।")
                + "\n\n" + licenseText);
        });
        helpMenuButton->setMenu(helpMenu);
        tools->addWidget(helpMenuButton);
        root->addWidget(toolbar);

        auto *workspace = new QHBoxLayout;
        workspace->setContentsMargins(0, 0, 0, 0);
        workspace->setSpacing(0);
        root->addLayout(workspace, 1);

        auto *controls = new QWidget;
        controls->setMinimumWidth(340);
        auto *left = new QVBoxLayout(controls);
        left->setContentsMargins(12, 13, 12, 12);
        left->setSpacing(9);
        pages = new QStackedWidget;
        pages->addWidget(controls);
        converter = new ConverterPanel;
        pages->addWidget(converter);
        workspace->addWidget(pages, 1);

        auto *display = new QWidget;
        display->setObjectName("display");
        auto *displayLayout = new QVBoxLayout(display);
        displayLayout->setContentsMargins(14, 8, 14, 8);
        displayLayout->setSpacing(0);
        expression = new Readout(20, 12);
        expression->setObjectName("expression");
        expression->setMinimumHeight(26);
        result = new Readout(57, 18);
        result->setObjectName("result");
        result->setMinimumHeight(58);
        displayLayout->addWidget(expression);
        displayLayout->addWidget(result, 1);
        left->addWidget(display);

        auto *memoryLayout = new QHBoxLayout;
        memoryLayout->setSpacing(1);
        const std::array<QString, 5> memoryKeys{"MC", "MR", "M+", "M−", "MS"};
        const std::array<Calculator::Memory, 5> memoryActions{
            Calculator::Memory::Clear, Calculator::Memory::Recall, Calculator::Memory::Add,
            Calculator::Memory::Subtract, Calculator::Memory::Store};
        for (size_t index = 0; index < memoryKeys.size(); ++index) {
            auto *button = new QPushButton(memoryKeys[index]);
            button->setProperty("memory", true);
            button->setFocusPolicy(Qt::TabFocus);
            connect(button, &QPushButton::clicked, this, [this, action = memoryActions[index]] {
                if (error.isEmpty() || action == Calculator::Memory::Clear
                                    || action == Calculator::Memory::Recall) {
                    if (action == Calculator::Memory::Recall && calculator.hasMemory())
                        error.clear();
                    perform([&] { calculator.memory(action); });
                }
            });
            memoryButtons[index] = button;
            memoryLayout->addWidget(button, 1);
        }
        left->addLayout(memoryLayout);

        scientificKeys = new QWidget;
        auto *scientificLayout = new QGridLayout(scientificKeys);
        scientificLayout->setContentsMargins(0, 0, 0, 0);
        scientificLayout->setSpacing(5);
        angleUnit = new QComboBox;
        angleUnit->setObjectName("angleUnit");
        angleUnit->addItems({QString(), QString()});
        angleUnit->setCurrentIndex(settings.value("radians", false).toBool() ? 1 : 0);
        angleUnit->setFocusPolicy(Qt::TabFocus);
        connect(angleUnit, &QComboBox::currentIndexChanged, this, [this](int index) {
            settings.setValue("radians", index == 1);
        });
        scientificLayout->addWidget(angleUnit, 0, 3, 1, 2);
        const std::array<std::pair<const char *, char>, 8> functions{{
            {"sin", 'a'}, {"cos", 'b'}, {"tan", 't'},
            {"log", 'l'}, {"ln", 'n'}, {"xʸ", '^'}, {"π", 'p'}, {"e", 'e'}
        }};
        for (size_t i = 0; i < functions.size(); ++i) {
            auto *button = new QPushButton(QString::fromUtf8(functions[i].first));
            button->setFocusPolicy(Qt::TabFocus);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            connect(button, &QPushButton::clicked, this, [this, key = functions[i].second] { press(key); });
            scientificButtons[i] = button;
            scientificLayout->addWidget(button, i < 3 ? 0 : 1, i < 3 ? i : i - 3);
        }
        for (int column = 0; column < 5; ++column)
            scientificLayout->setColumnStretch(column, 1);
        left->addWidget(scientificKeys);
        scientificKeys->hide();

        auto *grid = new QGridLayout;
        grid->setSpacing(5);
        const std::array<QString, 24> labels{
            "%", "CE", "C", "⌫", "1/x", "x²", "√x", "÷",
            "7", "8", "9", "×", "4", "5", "6", "−",
            "1", "2", "3", "+", "±", "0", ".", "="};
        const std::string keypad = "%ECBisr/789*456-123+S0.=";
        for (size_t index = 0; index < labels.size(); ++index) {
            auto *button = new QPushButton(labels[index]);
            button->setProperty("keypad", true);
            button->setFocusPolicy(Qt::TabFocus);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            button->setMinimumHeight(37);
            if (keypad[index] == '=')
                button->setObjectName("equals");
            connect(button, &QPushButton::clicked, this, [this, key = keypad[index]] { press(key); });
            keypadButtons[index] = button;
            grid->addWidget(button, index / 4, index % 4);
        }
        for (int row = 0; row < 6; ++row)
            grid->setRowStretch(row, 1);
        for (int column = 0; column < 4; ++column)
            grid->setColumnStretch(column, 1);
        left->addLayout(grid, 1);

        historyPanel = new QWidget;
        historyPanel->setObjectName("historyPanel");
        historyPanel->setFixedWidth(panelWidth);
        auto *right = new QVBoxLayout(historyPanel);
        right->setContentsMargins(17, 11, 16, 12);
        right->setSpacing(9);
        auto *heading = new QHBoxLayout;
        historyTitle = new QLabel;
        historyTitle->setObjectName("historyTitle");
        heading->addWidget(historyTitle);
        heading->addStretch();
        clearButton = new QToolButton;
        clearButton->setIcon(QIcon::fromTheme("edit-delete", style()->standardIcon(QStyle::SP_TrashIcon)));
        clearButton->setIconSize(QSize(20, 20));
        heading->addWidget(clearButton);
        right->addLayout(heading);
        historyList = new QListWidget;
        historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        historyList->setContextMenuPolicy(Qt::CustomContextMenu);
        right->addWidget(historyList, 1);
        emptyHistory = new QLabel;
        emptyHistory->setObjectName("emptyHistory");
        emptyHistory->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        right->addWidget(emptyHistory, 1);
        workspace->addWidget(historyPanel);

        connect(historyToggle, &QPushButton::toggled, this, [this](bool visible) {
            historyPanel->setVisible(visible);
            settings.setValue("showHistory", visible);
            resize(visible ? width() + panelWidth : width() - panelWidth, height());
        });
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
            pages->setCurrentIndex(index == converterMode ? 1 : 0);
            scientificKeys->setVisible(index == scientificMode);
            setFocus();
        });
        connect(language, &QComboBox::currentIndexChanged, this, [this](int index) {
            hindi = index == 1;
            settings.setValue("hindi", hindi);
            translate();
            refresh();
        });
        connect(clearButton, &QToolButton::clicked, this, [this] {
            if (QMessageBox::question(this, localized("Clear history", "इतिहास मिटाएँ"),
                    localized("Clear all saved calculations?", "सभी सहेजी गई गणनाएँ मिटाएँ?"))
                == QMessageBox::Yes) {
                history.clear();
                saveHistory();
            }
        });
        connect(historyList, &QListWidget::itemClicked, this, &CalculatorWindow::recall);
        connect(historyList, &QListWidget::itemActivated, this, &CalculatorWindow::recall);
        connect(historyList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
            auto *item = historyList->itemAt(point);
            if (!item)
                return;
            const auto record = item->data(Qt::UserRole).toMap();
            QMenu menu(this);
            menu.addAction(localized("Edit calculation", "गणना संपादित करें"), this,
                           [this, item] { recall(item); });
            menu.addAction(localized("Copy result", "परिणाम कॉपी करें"), this, [record] {
                copy(record.value("result").toString());
            });
            menu.exec(historyList->viewport()->mapToGlobal(point));
        });
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
        connect(copyShortcut, &QShortcut::activated, this, [this] {
            if (mode->currentIndex() != converterMode && error.isEmpty())
                copy(asText(calculator.display()));
        });

        const bool showHistory = settings.value("showHistory", true).toBool();
        historyToggle->blockSignals(true);
        historyToggle->setChecked(showHistory);
        historyToggle->blockSignals(false);
        historyPanel->setVisible(showHistory);
        resize(550 + (showHistory ? panelWidth : 0), 540);
        translate();
        refreshHistory();
        refresh();
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (event->type() != QEvent::KeyPress || !widget || widget->window() != this)
            return false;
        if (mode->currentIndex() == converterMode)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            return false;
        switch (key->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            if (widget == historyList || widget == language || widget == mode || widget == angleUnit
                || widget == helpMenuButton)
                return false;
            press('=');
            return true;
        case Qt::Key_Escape: press('C'); return true;
        case Qt::Key_Backspace: press('B'); return true;
        case Qt::Key_Delete: press('E'); return true;
        default: break;
        }
        const QString typed = key->text();
        if (typed.size() != 1)
            return false;
        const auto character = typed.front();
        const int digit = character.digitValue();
        if (digit >= 0 && digit <= 9) {
            press(static_cast<char>('0' + digit));
            return true;
        }
        if (QStringLiteral(".+-*/%=").contains(character)
            || (character == QChar('^') && mode->currentIndex() == scientificMode)) {
            press(character.toLatin1());
            return true;
        }
        return false;
    }

private:
    static constexpr int scientificMode = 1;
    static constexpr int converterMode = 2;
    QStackedWidget *pages;
    ConverterPanel *converter;
    QWidget *scientificKeys;
    QComboBox *angleUnit;
    std::array<QPushButton *, 8> scientificButtons;

    QToolButton *helpMenuButton;
    QAction *helpAction;
    QAction *aboutAction;
    QAction *creditsAction;
    QSettings settings;
    Calculator calculator;
    QVariantList history;
    bool hindi = false;
    QString error;
    Readout *expression;
    Readout *result;
    QComboBox *mode;
    QComboBox *language;
    QPushButton *historyToggle;
    QWidget *historyPanel;
    QLabel *historyTitle;
    QLabel *emptyHistory;
    QToolButton *clearButton;
    QListWidget *historyList;
    std::array<QPushButton *, 5> memoryButtons;
    std::array<QPushButton *, 24> keypadButtons;

    QString localized(const char *english, const char *translation) const
    {
        return QString::fromUtf8(hindi ? translation : english);
    }

    void showInformation(const QString &title, const QString &content, bool about = false)
    {
        QDialog dialog(this);
        dialog.setObjectName("informationDialog");
        dialog.setWindowTitle(title);
        auto *layout = new QVBoxLayout(&dialog);
        if (about) {
            auto *icon = new QLabel;
            icon->setPixmap(windowIcon().pixmap(56, 56));
            icon->setAlignment(Qt::AlignHCenter);
            layout->addWidget(icon);
        }
        auto *body = new QPlainTextEdit;
        body->setReadOnly(true);
        body->setPlainText(content);
        layout->addWidget(body);
        auto *buttons = new QDialogButtonBox;
        auto *close = buttons->addButton(localized("Close", "बंद करें"), QDialogButtonBox::RejectRole);
        close->setDefault(true);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (about) {
            auto *website = buttons->addButton("jatayuos.in", QDialogButtonBox::ActionRole);
            connect(website, &QPushButton::clicked, &dialog, [] {
                QDesktopServices::openUrl(QUrl(QStringLiteral("https://jatayuos.in")));
            });
        }
        layout->addWidget(buttons);
        dialog.resize(485, about ? 260 : 350);
        dialog.exec();
    }

    void showHelp()
    {
        showInformation(helpAction->text(), localized(
            "Standard\nUse the keypad or keyboard. Enter calculates, Backspace removes one digit, "
            "Delete clears the current entry, and Esc clears the calculation. Ctrl+C copies the result. "
            "Example: 1,500 + 10% = 1,650.\n\n"
            "Scientific\nChoose Deg or Rad, enter a value, then select a function. For example, "
            "enter 30 and press sin in Deg mode to get 0.5. For powers, enter the base, press xʸ, enter the exponent, then press =.\n\n"
            "Converter\nChoose a category and two units, then enter a value. The arrow button swaps the units. Copy result copies the number.\n\n"
            "History\nSelect a past calculation to edit it. Use the trash button to clear saved calculations.",
            "सामान्य\nकैलकुलेटर के बटन या कीबोर्ड का इस्तेमाल करें। Enter से परिणाम मिलता है। Backspace से पिछला अंक, "
            "Delete से मौजूदा संख्या और Esc से पूरी गणना मिटती है। Ctrl+C से परिणाम कॉपी करें। उदाहरण: 1,500 + 10% = 1,650।\n\n"
            "वैज्ञानिक\nडिग्री या रेडियन चुनें, मान दर्ज करें और फ़ंक्शन दबाएँ। उदाहरण के लिए, डिग्री में 30 डालकर sin दबाने पर 0.5 मिलता है। "
            "घात के लिए आधार दर्ज करें, xʸ दबाएँ, घातांक दर्ज करें, फिर = दबाएँ।\n\n"
            "इकाई परिवर्तक\nश्रेणी और दोनों इकाइयाँ चुनें, फिर मान दर्ज करें। तीर का बटन इकाइयाँ बदलता है। परिणाम कॉपी करें बटन से संख्या कॉपी होती है।\n\n"
            "इतिहास\nसंपादन के लिए पिछली गणना चुनें। सहेजी गई गणनाएँ मिटाने के लिए कूड़ेदान का बटन दबाएँ।"));
    }

    void translate()
    {
        setWindowTitle(localized("Jatayu Calculator", "जटायु कैलकुलेटर"));
        helpMenuButton->setText(localized("Help", "सहायता"));
        helpAction->setText(localized("Help", "सहायता"));
        aboutAction->setText(localized("About Jatayu Calculator", "जटायु कैलकुलेटर के बारे में"));
        creditsAction->setText(localized("License && Credits", "लाइसेंस और आभार"));
        mode->setItemText(0, localized("Standard", "सामान्य"));
        mode->setItemText(scientificMode, localized("Scientific", "वैज्ञानिक"));
        mode->setItemText(converterMode, localized("Converter", "इकाई परिवर्तक"));
        angleUnit->setItemText(0, localized("Deg", "डिग्री"));
        angleUnit->setItemText(1, localized("Rad", "रेडियन"));
        angleUnit->setAccessibleName(localized("Angle unit", "कोण की इकाई"));
        const std::array<QString, 8> scientificNames{
            localized("Sine", "ज्या"), localized("Cosine", "कोज्या"),
            localized("Tangent", "स्पर्शज्या"), localized("Base-10 logarithm", "आधार 10 का लघुगणक"),
            localized("Natural logarithm", "प्राकृतिक लघुगणक"), localized("Power", "घात"),
            localized("Pi", "पाई"), localized("Euler’s number", "ऑयलर की संख्या")};
        for (size_t index = 0; index < scientificNames.size(); ++index) {
            scientificButtons[index]->setAccessibleName(scientificNames[index]);
            scientificButtons[index]->setToolTip(scientificNames[index]);
        }
        converter->translate(hindi);
        mode->setAccessibleName(localized("Calculator mode", "कैलकुलेटर मोड"));
        language->setAccessibleName(localized("Language", "भाषा"));
        historyToggle->setText(localized("History", "इतिहास"));
        historyTitle->setText(localized("History", "इतिहास"));
        historyList->setAccessibleName(historyTitle->text());
        clearButton->setToolTip(localized("Clear history", "इतिहास मिटाएँ"));
        clearButton->setAccessibleName(clearButton->toolTip());
        emptyHistory->setText(localized("No calculations yet", "अभी कोई गणना नहीं है"));
        const std::array<QString, 5> descriptions{
            localized("Clear memory", "मेमोरी मिटाएँ"), localized("Recall memory", "मेमोरी से लें"),
            localized("Add to memory", "मेमोरी में जोड़ें"), localized("Subtract from memory", "मेमोरी से घटाएँ"),
            localized("Store in memory", "मेमोरी में सहेजें")};
        for (size_t i = 0; i < descriptions.size(); ++i) {
            memoryButtons[i]->setToolTip(descriptions[i]);
            memoryButtons[i]->setAccessibleName(descriptions[i]);
        }
        const std::array<QString, 8> operations{
            localized("Percent", "प्रतिशत"), localized("Clear entry", "मौजूदा अंक मिटाएँ"),
            localized("Clear all", "सब मिटाएँ"), localized("Backspace", "पिछला अंक मिटाएँ"),
            localized("Reciprocal", "व्युत्क्रम"), localized("Square", "वर्ग"),
            localized("Square root", "वर्गमूल"), localized("Divide", "भाग दें")};
        for (size_t i = 0; i < operations.size(); ++i) {
            keypadButtons[i]->setToolTip(operations[i]);
            keypadButtons[i]->setAccessibleName(operations[i]);
        }
        if (!error.isEmpty()) {
            calculator.clear();
            error.clear();
        }
    }

    static void copy(QString value)
    {
        value.remove(',');
        QApplication::clipboard()->setText(value);
    }

    void perform(const std::function<void()> &action)
    {
        try {
            action();
        } catch (jatayu::Error issue) {
            switch (issue) {
            case jatayu::Error::DivisionByZero:
                error = localized("Cannot divide by zero", "शून्य से भाग नहीं दे सकते");
                break;
            case jatayu::Error::NegativeRoot:
                error = localized("Negative square root", "ऋणात्मक संख्या का वर्गमूल");
                break;
            case jatayu::Error::Domain:
                error = localized("Undefined for this value", "इस मान के लिए अपरिभाषित");
                break;
            case jatayu::Error::Range:
                error = localized("Calculation out of range", "गणना सीमा से बाहर है");
                break;
            }
        }
        refresh();
    }

    void press(char key)
    {
        if (!error.isEmpty()) {
            if ((key < '0' || key > '9') && key != '.' && key != 'C' && key != 'E')
                return;
            calculator.clear();
            error.clear();
        }
        perform([&] {
            switch (key) {
            case 'C': calculator.clear(); break;
            case 'E': calculator.clearEntry(); break;
            case 'B': calculator.backspace(); break;
            case 'S': calculator.sign(); break;
            case '%': case 'i': case 'r': case 's': calculator.unary(key); break;
            case 'a': case 'b': case 't': case 'l': case 'n':
                calculator.scientific(key, angleUnit->currentIndex() == 1);
                break;
            case 'p': case 'e': calculator.constant(key); break;
            case '+': case '-': case '*': case '/': case '^': calculator.operation(key); break;
            case '=': {
                const auto snapshot = calculator.snapshot();
                if (calculator.equals()) {
                    auto description = asText(calculator.expression());
                    description.chop(2);
                    const QVariantMap record{
                        {"expression", description}, {"result", asText(calculator.display())},
                        {"snapshot", asText(snapshot)}, {"mode", mode->currentIndex()},
                        {"radians", angleUnit->currentIndex() == 1}};
                    if (snapshot.size() > Calculator::maxSnapshotBytes)
                        break;
                    history.prepend(record);
                    if (history.size() > historyLimit)
                        history.removeLast();
                    saveHistory();
                }
                break;
            }
            default: calculator.digit(key); break;
            }
        });
    }

    void refresh()
    {
        expression->showValue(asText(calculator.expression()));
        result->showValue(error.isEmpty() ? asText(calculator.display()) : error);
        memoryButtons[0]->setEnabled(calculator.hasMemory());
        memoryButtons[1]->setEnabled(calculator.hasMemory());
    }

    void saveHistory()
    {
        settings.setValue("history", history);
        refreshHistory();
    }

    void recall(QListWidgetItem *item)
    {
        const auto record = item->data(Qt::UserRole).toMap();
        if (calculator.restore(record.value("snapshot").toString().toStdString())) {
            mode->setCurrentIndex(record.value("mode").toInt() == scientificMode ? scientificMode : 0);
            if (mode->currentIndex() == scientificMode)
                angleUnit->setCurrentIndex(record.value("radians").toBool() ? 1 : 0);
            setFocus();
            error.clear();
            refresh();
        }
    }

    void refreshHistory()
    {
        historyList->clear();
        for (const auto &value : history) {
            const auto record = value.toMap();
            const auto description = record.value("expression").toString();
            const auto answer = record.value("result").toString();
            auto *item = new QListWidgetItem(historyList);
            item->setData(Qt::UserRole, record);
            item->setData(Qt::AccessibleTextRole, description + " = " + answer);
            item->setToolTip("<span>" + (description + " = " + answer).toHtmlEscaped() + "</span>");
            item->setSizeHint(QSize(0, 45));
            auto *row = new QWidget;
            row->setObjectName("historyRow");
            row->setAttribute(Qt::WA_TransparentForMouseEvents);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(1, 2, 3, 2);
            auto *entry = new Readout(17, 12);
            entry->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            entry->showValue(description);
            auto *output = new Readout(20, 12);
            output->setObjectName("historyResult");
            output->showValue(answer);
            layout->addWidget(entry, 3);
            layout->addWidget(output, 2);
            historyList->setItemWidget(item, row);
        }
        historyList->setVisible(!history.isEmpty());
        emptyHistory->setVisible(history.isEmpty());
        clearButton->setEnabled(!history.isEmpty());
    }
};

const char *theme = R"(
QWidget { color: #f4f7f5; }
QFrame#toolbarSeparator { background: #53635d; }
QWidget#window { background: #1a2422; }
QWidget#toolbar { background: #1d2825; border-bottom: 1px solid #3b4944; }
QWidget#display { background: #141d1b; border: 1px solid #50745b; border-radius: 7px; }
QLabel#expression { color: #c4cfca; }
QLabel#result, QLabel#conversionResult { font-weight: 600; }
QWidget#historyPanel { background: #182220; border-left: 1px solid #40504a; }
QLabel#historyTitle { font-size: 21px; font-weight: 600; }
QLabel#historyResult { font-weight: 600; }
QLabel#emptyHistory { color: #b0bdb8; padding-top: 16px; }
QListWidget { background: transparent; border: 0; outline: 0; }
QListWidget::item:hover, QListWidget::item:selected { background: #2d4138; }
QWidget#historyRow { border-bottom: 1px solid #485650; }
QPushButton, QComboBox, QToolButton, QLineEdit {
    background: #25312e; border: 1px solid #4b5b54; border-radius: 6px; padding: 5px 9px;
}
QPushButton[keypad="true"] { font-size: 24px; font-weight: 500; padding: 0; }
QPushButton[memory="true"] { font-size: 15px; padding: 7px 0; }
QPushButton#equals { background: #185b23; border-color: #328143; font-weight: 700; }
QPushButton#equals:hover { background: #216f30; }
QPushButton#equals:pressed { background: #12461b; }
QPushButton:hover, QComboBox:hover, QToolButton:hover { background: #33433c; }
QPushButton:pressed, QToolButton:pressed { background: #1a2923; }
QPushButton:focus, QComboBox:focus, QToolButton:focus, QLineEdit:focus { border-color: #82ad8c; }
QPushButton:disabled, QToolButton:disabled { color: #a2afa9; }
QPlainTextEdit {
    background: #141d1b; border: 1px solid #4b5b54; border-radius: 6px; padding: 8px;
}
QComboBox QAbstractItemView, QMenu {
    background: #25312e; color: #f4f7f5; selection-background-color: #315c40;
}
)";

}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName("JatayuOS");
    QApplication::setApplicationName("Jatayu Calculator");
    QApplication::setApplicationVersion("1.1.1");
    QApplication::setDesktopFileName("in.jatayuos.Calculator");
    auto font = application.font();
    font.setPixelSize(15);
    application.setFont(font);
    auto palette = application.palette();
    palette.setColor(QPalette::Window, QColor("#1d2825"));
    palette.setColor(QPalette::WindowText, QColor("#f4f7f5"));
    palette.setColor(QPalette::Text, QColor("#f4f7f5"));
    palette.setColor(QPalette::Base, QColor("#182220"));
    palette.setColor(QPalette::Button, QColor("#25312e"));
    palette.setColor(QPalette::ButtonText, QColor("#f4f7f5"));
    palette.setColor(QPalette::Highlight, QColor("#315c40"));
    palette.setColor(QPalette::HighlightedText, QColor("#f4f7f5"));
    application.setPalette(palette);
    application.setStyleSheet(QString::fromUtf8(theme));
    CalculatorWindow window;
    window.show();
    window.setFocus();
    return application.exec();
}
