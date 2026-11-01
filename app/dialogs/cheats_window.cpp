#include "dialogs/cheats_window.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace cutenes::app {

namespace {

QString hex(unsigned value, int digits)
{
    return QLatin1Char('$') + QString::number(value, 16).rightJustified(digits, QLatin1Char('0')).toUpper();
}

QTableWidgetItem* cell(const QString& text, bool editable = false)
{
    auto* item = new QTableWidgetItem(text);
    if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidget* table(QWidget* parent, const char* name, const QStringList& headers)
{
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    t->setObjectName(QString::fromLatin1(name));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

} // namespace

std::optional<unsigned> parseHex(const QString& text, unsigned max)
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1Char('$'))) {
        t = t.mid(1);
    } else if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        t = t.mid(2);
    }
    bool ok = false;
    const unsigned value = t.toUInt(&ok, 16);
    if (!ok || t.isEmpty() || value > max) return std::nullopt;
    return value;
}

std::optional<unsigned> parseNumber(const QString& text, unsigned max)
{
    QString t = text.trimmed();
    int base = 10;
    if (t.startsWith(QLatin1Char('$'))) {
        t = t.mid(1);
        base = 16;
    } else if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        t = t.mid(2);
        base = 16;
    }
    bool ok = false;
    const unsigned value = t.toUInt(&ok, base);
    if (!ok || t.isEmpty() || value > max) return std::nullopt;
    return value;
}

// --- Cheats --------------------------------------------------------------------

CheatsWindow::CheatsWindow(CommandRunner run, QWidget* parent) : QDialog(parent), m_run(std::move(run))
{
    setObjectName(QStringLiteral("cheatsWindow"));
    setWindowTitle(tr("Cheats"));
    m_table = table(this, "cheatTable", {tr("On"), tr("Label"), tr("Kind"), tr("Address or code"), tr("Value")});
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_filling || item->column() != 0) return;
        m_run(QStringLiteral("gui.set_cheat_enabled"),
            {{QStringLiteral("index"), item->row()}, {QStringLiteral("enabled"), item->checkState() == Qt::Checked}});
    });

    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("cheatKind"));
    m_kind->addItem(tr("Freeze (every frame)"), QStringLiteral("freeze"));
    m_kind->addItem(tr("Set (once)"), QStringLiteral("set"));
    m_kind->addItem(tr("Game Genie"), QStringLiteral("game_genie"));
    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("cheatAddress"));
    m_address->setPlaceholderText(QStringLiteral("$0075"));
    m_value = new QLineEdit(this);
    m_value->setObjectName(QStringLiteral("cheatValue"));
    m_value->setPlaceholderText(QStringLiteral("$09"));
    m_code = new QLineEdit(this);
    m_code->setObjectName(QStringLiteral("cheatCode"));
    m_code->setPlaceholderText(QStringLiteral("SXIOPO"));
    m_label = new QLineEdit(this);
    m_label->setObjectName(QStringLiteral("cheatLabel"));
    const auto kindChanged = [this] {
        const bool genie = m_kind->currentData().toString() == QLatin1String("game_genie");
        m_address->setEnabled(!genie);
        m_value->setEnabled(!genie);
        m_code->setEnabled(genie);
    };
    connect(m_kind, &QComboBox::currentIndexChanged, this, kindChanged);
    kindChanged();

    auto* add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("addCheat"));
    connect(add, &QPushButton::clicked, this, &CheatsWindow::add);
    auto* remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("removeCheat"));
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_table->currentRow() < 0) return;
        m_run(QStringLiteral("gui.remove_cheat"), {{QStringLiteral("index"), m_table->currentRow()}});
    });

    auto* form = new QFormLayout;
    form->addRow(tr("Kind"), m_kind);
    form->addRow(tr("Address"), m_address);
    form->addRow(tr("Value"), m_value);
    form->addRow(tr("Code"), m_code);
    form->addRow(tr("Label"), m_label);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addLayout(form);
    layout->addLayout(buttons);
    resize(520, 420);
}

void CheatsWindow::add()
{
    QVariantMap args{{QStringLiteral("kind"), m_kind->currentData()}, {QStringLiteral("label"), m_label->text()}};
    if (m_kind->currentData().toString() == QLatin1String("game_genie")) {
        args.insert(QStringLiteral("code"), m_code->text().trimmed());
    } else {
        const auto address = parseNumber(m_address->text(), 0xFFFF);
        const auto value = parseNumber(m_value->text(), 0xFF);
        if (!address || !value) {
            m_address->setFocus();
            return; // the command would refuse it too; nothing to send
        }
        args.insert(QStringLiteral("address"), *address);
        args.insert(QStringLiteral("value"), *value);
    }
    m_run(QStringLiteral("gui.add_cheat"), args);
    m_code->clear();
    m_label->clear();
}

void CheatsWindow::refresh()
{
    m_run(QStringLiteral("gui.list_cheats"), {}).then(this, [this](const api::Result& result) {
        if (result.isError()) return;
        const api::Json& cheats = result.value["cheats"];
        m_filling = true;
        m_table->setRowCount(static_cast<int>(cheats.size()));
        for (int row = 0; row < static_cast<int>(cheats.size()); ++row) {
            const api::Json& c = cheats[static_cast<std::size_t>(row)];
            auto* on = cell({});
            on->setFlags(on->flags() | Qt::ItemIsUserCheckable);
            on->setCheckState(c["enabled"].get<bool>() ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(row, 0, on);
            m_table->setItem(row, 1, cell(QString::fromStdString(c["label"].get<std::string>())));
            const QString kind = QString::fromStdString(c["kind"].get<std::string>());
            const bool genie = kind == QLatin1String("game_genie");
            m_table->setItem(row, 2, cell(genie ? tr("Game Genie") : kind == QLatin1String("set") ? tr("Set") : tr("Freeze")));
            m_table->setItem(row, 3, cell(genie ? QString::fromStdString(c["code"].get<std::string>())
                                                : hex(c["address"].get<unsigned>(), 4)));
            m_table->setItem(row, 4, cell(genie ? QString() : hex(c["value"].get<unsigned>(), 2)));
        }
        m_filling = false;
    });
}

// --- Cheat search ----------------------------------------------------------------

CheatSearchWindow::CheatSearchWindow(CommandRunner run, QWidget* parent) : QDialog(parent), m_run(std::move(run))
{
    setObjectName(QStringLiteral("cheatSearchWindow"));
    setWindowTitle(tr("Cheat Search"));

    m_size = new QComboBox(this);
    m_size->setObjectName(QStringLiteral("searchSize"));
    m_size->addItem(tr("1 byte"), 1);
    m_size->addItem(tr("2 bytes"), 2);
    auto* reset = new QPushButton(tr("New Search"), this);
    reset->setObjectName(QStringLiteral("searchReset"));
    connect(reset, &QPushButton::clicked, this, [this] {
        m_run(QStringLiteral("gui.cheat_search_reset"), {{QStringLiteral("size"), m_size->currentData()}})
            .then(this, [this](const api::Result&) { refresh(); });
    });

    m_filter = new QComboBox(this);
    m_filter->setObjectName(QStringLiteral("searchFilter"));
    m_filter->addItem(tr("Equal to"), QStringLiteral("equal"));
    m_filter->addItem(tr("Not equal to"), QStringLiteral("not_equal"));
    m_filter->addItem(tr("Greater than"), QStringLiteral("greater"));
    m_filter->addItem(tr("Less than"), QStringLiteral("less"));
    m_filter->addItem(tr("Changed"), QStringLiteral("changed"));
    m_filter->addItem(tr("Unchanged"), QStringLiteral("unchanged"));
    m_filterValue = new QLineEdit(this);
    m_filterValue->setObjectName(QStringLiteral("searchValue"));
    m_filterValue->setPlaceholderText(tr("value (blank: the last snapshot)"));
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] {
        const QString f = m_filter->currentData().toString();
        m_filterValue->setEnabled(f != QLatin1String("changed") && f != QLatin1String("unchanged"));
    });
    auto* apply = new QPushButton(tr("Filter"), this);
    apply->setObjectName(QStringLiteral("searchApply"));
    connect(apply, &QPushButton::clicked, this, &CheatSearchWindow::apply);
    connect(m_filterValue, &QLineEdit::returnPressed, this, &CheatSearchWindow::apply);
    m_count = new QLabel(tr("Start a new search"), this);
    m_count->setObjectName(QStringLiteral("searchCount"));

    m_results = table(this, "searchResults", {tr("Address"), tr("Value"), tr("Previous")});
    connect(m_results, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_filling || item->column() != 1) return;
        const auto address = parseNumber(m_results->item(item->row(), 0)->text(), 0xFFFF);
        if (address) write(*address, m_searchSize, item->text());
    });

    auto* freeze = new QPushButton(tr("Freeze"), this);
    freeze->setObjectName(QStringLiteral("searchFreeze"));
    freeze->setToolTip(tr("Add the selected address as a freeze cheat at its value now"));
    connect(freeze, &QPushButton::clicked, this, [this] {
        const int row = m_results->currentRow();
        if (row < 0) return;
        const auto address = parseNumber(m_results->item(row, 0)->text(), 0xFFFF);
        const auto value = parseNumber(m_results->item(row, 1)->text(), 0xFFFF);
        if (!address || !value) return;
        m_run(QStringLiteral("gui.add_cheat"), {{QStringLiteral("kind"), QStringLiteral("freeze")},
            {QStringLiteral("address"), *address}, {QStringLiteral("value"), *value & 0xFF},
            {QStringLiteral("label"), m_watchLabel->text()}});
    });
    m_watchLabel = new QLineEdit(this);
    m_watchLabel->setObjectName(QStringLiteral("watchLabel"));
    m_watchLabel->setPlaceholderText(tr("label"));
    auto* watch = new QPushButton(tr("Watch"), this);
    watch->setObjectName(QStringLiteral("searchWatch"));
    connect(watch, &QPushButton::clicked, this, [this] {
        const int address = selectedAddress();
        if (address < 0) return;
        m_run(QStringLiteral("gui.add_watch"), {{QStringLiteral("address"), address},
            {QStringLiteral("size"), m_searchSize}, {QStringLiteral("label"), m_watchLabel->text()}});
        m_watchLabel->clear();
    });

    m_watches = table(this, "watchList", {tr("Label"), tr("Address"), tr("Value")});
    connect(m_watches, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_filling || item->column() != 2) return;
        const auto address = parseNumber(m_watches->item(item->row(), 1)->text(), 0xFFFF);
        const int size = m_watches->item(item->row(), 1)->data(Qt::UserRole).toInt();
        if (address) write(*address, size, item->text());
    });
    auto* unwatch = new QPushButton(tr("Remove Watch"), this);
    unwatch->setObjectName(QStringLiteral("removeWatch"));
    connect(unwatch, &QPushButton::clicked, this, [this] {
        if (m_watches->currentRow() >= 0) {
            m_run(QStringLiteral("gui.remove_watch"), {{QStringLiteral("index"), m_watches->currentRow()}});
        }
    });

    auto* top = new QHBoxLayout;
    top->addWidget(m_size);
    top->addWidget(reset);
    top->addStretch();
    top->addWidget(m_count);
    auto* filters = new QHBoxLayout;
    filters->addWidget(m_filter);
    filters->addWidget(m_filterValue, 1);
    filters->addWidget(apply);
    auto* actions = new QHBoxLayout;
    actions->addWidget(m_watchLabel, 1);
    actions->addWidget(watch);
    actions->addWidget(freeze);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(filters);
    layout->addWidget(m_results, 2);
    layout->addLayout(actions);
    layout->addWidget(new QLabel(tr("Watch list (type a value to change it)"), this));
    layout->addWidget(m_watches, 1);
    layout->addWidget(unwatch, 0, Qt::AlignLeft);
    resize(460, 600);

    // US8-2: live values, at least ten times a second.
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &CheatSearchWindow::refresh);
}

void CheatSearchWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_timer->start();
    refresh();
}

void CheatSearchWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void CheatSearchWindow::apply()
{
    QVariantMap args{{QStringLiteral("filter"), m_filter->currentData()}};
    if (m_filterValue->isEnabled() && !m_filterValue->text().trimmed().isEmpty()) {
        const auto value = parseNumber(m_filterValue->text(), m_searchSize == 2 ? 0xFFFF : 0xFF);
        if (!value) {
            m_filterValue->selectAll();
            return;
        }
        args.insert(QStringLiteral("value"), *value);
    }
    m_run(QStringLiteral("gui.cheat_search_filter"), args).then(this, [this](const api::Result&) { refresh(); });
}

void CheatSearchWindow::write(unsigned address, int size, const QString& text)
{
    const auto value = parseNumber(text, size == 2 ? 0xFFFF : 0xFF);
    if (!value) return;
    QByteArray bytes;
    bytes.append(static_cast<char>(*value & 0xFF));
    if (size == 2) bytes.append(static_cast<char>(*value >> 8));
    m_run(QStringLiteral("core.write_memory"),
        {{QStringLiteral("addr"), address}, {QStringLiteral("bytes"), QString::fromLatin1(bytes.toBase64())}});
}

int CheatSearchWindow::selectedAddress() const
{
    const int row = m_results->currentRow();
    if (row < 0) return -1;
    const auto address = parseNumber(m_results->item(row, 0)->text(), 0xFFFF);
    return address ? static_cast<int>(*address) : -1;
}

void CheatSearchWindow::refresh()
{
    // One refresh at a time, and none while a value is being typed.
    const QWidget* focus = QApplication::focusWidget();
    const auto editing = [focus](const QTableWidget* t) { return focus && focus != t && t->isAncestorOf(focus); };
    if (m_inFlight || editing(m_results) || editing(m_watches)) return;
    m_inFlight = true;
    m_run(QStringLiteral("gui.cheat_search_results"), {{QStringLiteral("max"), 500}})
        .then(this, [this](const api::Result& result) {
            if (!result.isError()) {
                const api::Json& v = result.value;
                m_searchSize = std::max(1, v["size"].get<int>());
                const auto candidates = v["candidates"].get<std::size_t>();
                const api::Json& rows = v["results"];
                m_count->setText(v["size"].get<int>() == 0 ? tr("Start a new search")
                        : candidates > rows.size()          ? tr("%1 addresses (first %2 shown)").arg(candidates).arg(rows.size())
                                                            : tr("%n address(es)", nullptr, static_cast<int>(candidates)));
                const int digits = m_searchSize * 2;
                const int keep = m_results->currentRow();
                m_filling = true;
                m_results->setRowCount(static_cast<int>(rows.size()));
                for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
                    const api::Json& r = rows[static_cast<std::size_t>(row)];
                    m_results->setItem(row, 0, cell(hex(r["address"].get<unsigned>(), 4)));
                    m_results->setItem(row, 1, cell(hex(r["value"].get<unsigned>(), digits), true));
                    m_results->setItem(row, 2, cell(hex(r["previous"].get<unsigned>(), digits)));
                }
                if (keep >= 0 && keep < m_results->rowCount()) m_results->setCurrentCell(keep, 0);
                m_filling = false;
            }
            return m_run(QStringLiteral("gui.list_watches"), {});
        })
        .unwrap()
        .then(this, [this](const api::Result& result) {
            m_inFlight = false;
            if (result.isError()) return;
            const api::Json& watches = result.value["watches"];
            const int keep = m_watches->currentRow();
            m_filling = true;
            m_watches->setRowCount(static_cast<int>(watches.size()));
            for (int row = 0; row < static_cast<int>(watches.size()); ++row) {
                const api::Json& w = watches[static_cast<std::size_t>(row)];
                const int size = w["size"].get<int>();
                m_watches->setItem(row, 0, cell(QString::fromStdString(w["label"].get<std::string>())));
                auto* address = cell(hex(w["address"].get<unsigned>(), 4));
                address->setData(Qt::UserRole, size);
                m_watches->setItem(row, 1, address);
                m_watches->setItem(row, 2, cell(hex(w["value"].get<unsigned>(), size * 2), true));
            }
            if (keep >= 0 && keep < m_watches->rowCount()) m_watches->setCurrentCell(keep, 0);
            m_filling = false;
        });
}

} // namespace cutenes::app
