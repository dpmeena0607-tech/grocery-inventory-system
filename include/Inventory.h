#ifndef INVENTORY_H
#define INVENTORY_H

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

struct Date {
    int year = 0, month = 0, day = 0;
};
inline bool operator<(const Date& a, const Date& b) {
    return std::tie(a.year, a.month, a.day) < std::tie(b.year, b.month, b.day);
}
inline bool operator==(const Date& a, const Date& b) {
    return std::tie(a.year, a.month, a.day) == std::tie(b.year, b.month, b.day);
}

// Strict "YYYY-MM-DD" for real calendar dates (leap years handled), years 1900-2999.
bool parseDate(const std::string& text, Date* out);
std::string formatDate(const Date& date);
long long daysBetween(const Date& from, const Date& to);  // to - from; negative if `to` is earlier

// "49", "49.5" or "49.50" -> paise. Rejects signs, extra decimals, letters, and very large values.
bool parseMoney(const std::string& text, long long* paise);
std::string formatMoney(long long paise);  // 4950 -> "49.50"

struct Product {
    std::string sku;       // upper case, e.g. "RICE-5KG"
    std::string name;
    std::string category;  // lower case, e.g. "rice"
    long long pricePaise = 0;
    int quantity = 0;
    int reorderLevel = 0;  // "low stock" when quantity <= reorderLevel
    std::optional<Date> expiry;
};

struct Result {
    bool ok = false;
    std::string message;
};

struct PurchaseLine {
    std::string sku, name;
    int quantity = 0;
    long long unitPaise = 0;
};
struct PurchasePlan {
    bool ok = false;
    std::string message;
    std::vector<PurchaseLine> lines;
    long long totalPaise = 0;
};

// The stock list. Every change is written to the CSV file before it takes effect, and is undone
// completely if the write fails, so memory and file never disagree.
class Inventory {
public:
    static constexpr int kMaxQuantity = 1000000;
    static constexpr long long kMaxPricePaise = 100000000;  // Rs 1,000,000

    explicit Inventory(std::string path);

    // A missing file means an empty inventory. Damaged or duplicate lines are skipped and counted.
    bool load(std::string* error = nullptr);
    int skippedLines() const { return skipped_; }

    Result add(Product product);
    Result remove(const std::string& sku);
    Result receive(const std::string& sku, int quantity, std::optional<Date> newExpiry = std::nullopt);
    Result issue(const std::string& sku, int quantity);
    Result setPrice(const std::string& sku, long long paise);

    const Product* find(const std::string& sku) const;
    std::vector<const Product*> all() const;                                  // sorted by SKU
    std::vector<const Product*> lowStock() const;                             // quantity <= reorderLevel
    std::vector<const Product*> expiringWithin(const Date& today, int days) const;  // includes already expired
    std::vector<const Product*> search(const std::string& text) const;       // SKU, name or category
    long long totalValuePaise() const;

    // Buy `quantity` units from a category, cheapest first (ties: earliest expiry first, then SKU).
    // Expired and out-of-stock products are skipped. All or nothing: if the category cannot supply
    // the full amount, nothing is bought.
    PurchasePlan planPurchase(const std::string& category, int quantity, const Date& today) const;
    PurchasePlan purchase(const std::string& category, int quantity, const Date& today);

    static std::string normalizeSku(const std::string& sku);

private:
    using Items = std::map<std::string, Product>;
    bool save(const Items& items) const;
    Result commit(Items next, const std::string& okMessage);

    std::string path_;
    Items items_;
    int skipped_ = 0;
};

#endif  // INVENTORY_H
