#include "Inventory.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

constexpr std::size_t kMaxNameLength = 60;
constexpr std::size_t kMaxCategoryLength = 30;
constexpr std::size_t kMaxSkuLength = 20;

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
// Replaces characters that would break the CSV file, then trims and limits the length.
std::string cleanText(std::string s, std::size_t maxLength) {
    for (char& c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    std::string collapsed;  // "a   b" -> "a b"
    for (char c : trim(s)) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed += c;
    }
    if (collapsed.size() > maxLength) collapsed.resize(maxLength);
    return trim(collapsed);
}
bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}
bool parseCount(const std::string& s, long long* out) {  // non-negative whole number, at most 9 digits
    if (!allDigits(s) || s.size() > 9) return false;
    *out = std::stoll(s);
    return true;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == sep) { parts.push_back(cur); cur.clear(); } else { cur += c; }
    }
    parts.push_back(cur);
    return parts;
}
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int daysInMonth(int y, int m) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && isLeap(y) ? 29 : days[m - 1];
}
long long daysFromCivil(int y, int m, int d) {  // days since 1970-01-01
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
// Normalises and checks a product. Returns false and fills *why if a field is unusable.
bool normalizeProduct(Product& p, std::string* why) {
    p.sku = Inventory::normalizeSku(p.sku);
    p.name = cleanText(p.name, kMaxNameLength);
    p.category = lower(cleanText(p.category, kMaxCategoryLength));
    const bool skuOk = !p.sku.empty() && p.sku.size() <= kMaxSkuLength &&
                       std::all_of(p.sku.begin(), p.sku.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
    if (!skuOk) { *why = "SKU must be 1-20 letters, digits or '-'."; return false; }
    if (p.name.empty()) { *why = "Name is required."; return false; }
    if (p.category.empty()) { *why = "Category is required."; return false; }
    if (p.pricePaise < 1 || p.pricePaise > Inventory::kMaxPricePaise) { *why = "Price must be between Rs 0.01 and Rs 1000000."; return false; }
    if (p.quantity < 0 || p.quantity > Inventory::kMaxQuantity) { *why = "Quantity must be between 0 and 1000000."; return false; }
    if (p.reorderLevel < 0 || p.reorderLevel > Inventory::kMaxQuantity) { *why = "Reorder level must be between 0 and 1000000."; return false; }
    return true;
}

bool sellable(const Product& p, const Date& today) {
    return p.quantity > 0 && (!p.expiry || !(*p.expiry < today));
}

}  // namespace

// ---- dates and money -------------------------------------------------------------------------

bool parseDate(const std::string& text, Date* out) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    const std::string y = text.substr(0, 4), m = text.substr(5, 2), d = text.substr(8, 2);
    if (!allDigits(y) || !allDigits(m) || !allDigits(d)) return false;
    Date date{std::stoi(y), std::stoi(m), std::stoi(d)};
    if (date.year < 1900 || date.month < 1 || date.month > 12 || date.day < 1 ||
        date.day > daysInMonth(date.year, date.month)) {
        return false;
    }
    *out = date;
    return true;
}

std::string formatDate(const Date& d) {
    std::ostringstream os;
    os << std::setfill('0') << std::setw(4) << d.year << '-' << std::setw(2) << d.month << '-' << std::setw(2) << d.day;
    return os.str();
}

long long daysBetween(const Date& from, const Date& to) {
    return daysFromCivil(to.year, to.month, to.day) - daysFromCivil(from.year, from.month, from.day);
}

bool parseMoney(const std::string& text, long long* paise) {
    const std::string s = trim(text);
    const std::size_t dot = s.find('.');
    const std::string whole = s.substr(0, dot);
    std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (!allDigits(whole) || whole.size() > 9) return false;
    if (dot != std::string::npos && (!allDigits(frac) || frac.size() > 2)) return false;
    while (frac.size() < 2) frac += '0';
    *paise = std::stoll(whole) * 100 + std::stoll(frac);
    return true;
}

std::string formatMoney(long long paise) {
    std::ostringstream os;
    os << paise / 100 << '.' << std::setfill('0') << std::setw(2) << paise % 100;
    return os.str();
}

// ---- storage ---------------------------------------------------------------------------------

std::string Inventory::normalizeSku(const std::string& sku) { return upper(trim(sku)); }

Inventory::Inventory(std::string path) : path_(std::move(path)) {}

// File format: header line, then  sku,name,category,price_paise,quantity,reorder_level,expiry
bool Inventory::save(const Items& items) const {
    std::error_code ec;
    const std::filesystem::path target(path_);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);

    // Write a temporary file and rename it over the real one, so a crash cannot leave half a file.
    const std::string temporary = path_ + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "sku,name,category,price_paise,quantity,reorder_level,expiry\n";
        for (const auto& entry : items) {
            const Product& p = entry.second;
            out << p.sku << ',' << p.name << ',' << p.category << ',' << p.pricePaise << ',' << p.quantity << ','
                << p.reorderLevel << ',' << (p.expiry ? formatDate(*p.expiry) : "") << '\n';
        }
        out.flush();
        if (!out) return false;
    }
    std::filesystem::rename(temporary, target, ec);
    return !ec;
}

bool Inventory::load(std::string* error) {
    items_.clear();
    skipped_ = 0;
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return true;

    std::ifstream in(path_);
    if (!in) {
        if (error != nullptr) *error = "Cannot read " + path_ + ".";
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line.rfind("sku,", 0) == 0) continue;
        const std::vector<std::string> f = split(line, ',');
        Product p;
        long long price = 0, quantity = 0, reorder = 0;
        std::string why;
        bool ok = f.size() == 7 && parseCount(f[3], &price) && parseCount(f[4], &quantity) && parseCount(f[5], &reorder);
        if (ok) {
            p.sku = f[0]; p.name = f[1]; p.category = f[2];
            p.pricePaise = price; p.quantity = static_cast<int>(quantity); p.reorderLevel = static_cast<int>(reorder);
            if (!f[6].empty()) {
                Date d;
                if (parseDate(f[6], &d)) p.expiry = d; else ok = false;
            }
        }
        ok = ok && normalizeProduct(p, &why) && items_.count(p.sku) == 0;
        if (ok) items_[p.sku] = p; else ++skipped_;
    }
    return true;
}

Result Inventory::commit(Items next, const std::string& okMessage) {
    if (!save(next)) return {false, "Could not save the change, so nothing was changed."};
    items_ = std::move(next);
    return {true, okMessage};
}

// ---- changes ---------------------------------------------------------------------------------

Result Inventory::add(Product product) {
    std::string why;
    if (!normalizeProduct(product, &why)) return {false, why};
    if (items_.count(product.sku) != 0) return {false, "SKU " + product.sku + " already exists."};
    Items next = items_;
    const std::string sku = product.sku;
    next[sku] = std::move(product);
    return commit(std::move(next), "Added " + sku + ".");
}

Result Inventory::remove(const std::string& sku) {
    const std::string key = normalizeSku(sku);
    if (items_.count(key) == 0) return {false, "No product with SKU " + key + "."};
    Items next = items_;
    next.erase(key);
    return commit(std::move(next), "Removed " + key + ".");
}

Result Inventory::receive(const std::string& sku, int quantity, std::optional<Date> newExpiry) {
    const std::string key = normalizeSku(sku);
    const auto it = items_.find(key);
    if (it == items_.end()) return {false, "No product with SKU " + key + "."};
    if (quantity < 1 || quantity > kMaxQuantity) return {false, "Quantity must be between 1 and 1000000."};
    if (static_cast<long long>(it->second.quantity) + quantity > kMaxQuantity) return {false, "That would exceed the maximum stock of 1000000."};
    Items next = items_;
    next[key].quantity += quantity;
    if (newExpiry) next[key].expiry = newExpiry;
    return commit(std::move(next), "Received " + std::to_string(quantity) + " of " + key + ".");
}

Result Inventory::issue(const std::string& sku, int quantity) {
    const std::string key = normalizeSku(sku);
    const auto it = items_.find(key);
    if (it == items_.end()) return {false, "No product with SKU " + key + "."};
    if (quantity < 1 || quantity > kMaxQuantity) return {false, "Quantity must be between 1 and 1000000."};
    if (quantity > it->second.quantity) return {false, "Only " + std::to_string(it->second.quantity) + " of " + key + " in stock."};
    Items next = items_;
    next[key].quantity -= quantity;
    return commit(std::move(next), "Sold " + std::to_string(quantity) + " of " + key + ".");
}

Result Inventory::setPrice(const std::string& sku, long long paise) {
    const std::string key = normalizeSku(sku);
    if (items_.count(key) == 0) return {false, "No product with SKU " + key + "."};
    if (paise < 1 || paise > kMaxPricePaise) return {false, "Price must be between Rs 0.01 and Rs 1000000."};
    Items next = items_;
    next[key].pricePaise = paise;
    return commit(std::move(next), "Price of " + key + " is now Rs " + formatMoney(paise) + ".");
}

// ---- queries ---------------------------------------------------------------------------------

const Product* Inventory::find(const std::string& sku) const {
    const auto it = items_.find(normalizeSku(sku));
    return it == items_.end() ? nullptr : &it->second;
}

std::vector<const Product*> Inventory::all() const {
    std::vector<const Product*> out;
    for (const auto& entry : items_) out.push_back(&entry.second);
    return out;
}

std::vector<const Product*> Inventory::lowStock() const {
    std::vector<const Product*> out;
    for (const auto& entry : items_) {
        if (entry.second.quantity <= entry.second.reorderLevel) out.push_back(&entry.second);
    }
    return out;
}

std::vector<const Product*> Inventory::expiringWithin(const Date& today, int days) const {
    std::vector<const Product*> out;
    for (const auto& entry : items_) {
        if (entry.second.expiry && daysBetween(today, *entry.second.expiry) <= days) out.push_back(&entry.second);
    }
    std::sort(out.begin(), out.end(), [](const Product* a, const Product* b) {
        return std::make_pair(*a->expiry, a->sku) < std::make_pair(*b->expiry, b->sku);
    });
    return out;
}

std::vector<const Product*> Inventory::search(const std::string& text) const {
    std::vector<const Product*> out;
    const std::string needle = lower(trim(text));
    if (needle.empty()) return out;
    for (const auto& entry : items_) {
        const Product& p = entry.second;
        if (lower(p.sku).find(needle) != std::string::npos || lower(p.name).find(needle) != std::string::npos ||
            p.category.find(needle) != std::string::npos) {
            out.push_back(&p);
        }
    }
    return out;
}

long long Inventory::totalValuePaise() const {
    long long total = 0;
    for (const auto& entry : items_) total += entry.second.pricePaise * entry.second.quantity;
    return total;
}

PurchasePlan Inventory::planPurchase(const std::string& categoryText, int quantity, const Date& today) const {
    PurchasePlan plan;
    const std::string category = lower(cleanText(categoryText, kMaxCategoryLength));
    if (category.empty()) { plan.message = "Category is required."; return plan; }
    if (quantity < 1 || quantity > kMaxQuantity) { plan.message = "Quantity must be between 1 and 1000000."; return plan; }

    std::vector<const Product*> candidates;
    long long available = 0;
    for (const auto& entry : items_) {
        if (entry.second.category == category && sellable(entry.second, today)) {
            candidates.push_back(&entry.second);
            available += entry.second.quantity;
        }
    }
    if (available < quantity) {
        plan.message = "Only " + std::to_string(available) + " unit(s) available in '" + category + "'.";
        return plan;
    }

    const Date never{9999, 12, 31};  // products without an expiry date go last among equal prices
    std::sort(candidates.begin(), candidates.end(), [&](const Product* a, const Product* b) {
        return std::make_tuple(a->pricePaise, a->expiry.value_or(never), a->sku) <
               std::make_tuple(b->pricePaise, b->expiry.value_or(never), b->sku);
    });

    int remaining = quantity;
    for (const Product* p : candidates) {
        if (remaining == 0) break;
        const int take = std::min(remaining, p->quantity);
        plan.lines.push_back({p->sku, p->name, take, p->pricePaise});
        plan.totalPaise += static_cast<long long>(take) * p->pricePaise;
        remaining -= take;
    }
    plan.ok = true;
    plan.message = "OK";
    return plan;
}

PurchasePlan Inventory::purchase(const std::string& category, int quantity, const Date& today) {
    PurchasePlan plan = planPurchase(category, quantity, today);
    if (!plan.ok) return plan;
    Items next = items_;
    for (const PurchaseLine& line : plan.lines) next[line.sku].quantity -= line.quantity;
    const Result saved = commit(std::move(next), "Purchase complete.");
    plan.ok = saved.ok;
    plan.message = saved.message;
    return plan;
}
