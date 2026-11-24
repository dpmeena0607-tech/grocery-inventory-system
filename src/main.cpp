#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

#include "Inventory.h"

namespace {

constexpr int kSoonDays = 7;

Date today() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    return Date{local->tm_year + 1900, local->tm_mon + 1, local->tm_mday};
}

// Prints the prompt and reads one line. Returns false when the input has ended.
bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    return static_cast<bool>(std::getline(std::cin, out));
}

bool readInt(const std::string& prompt, int low, int high, int& out) {
    std::string line;
    while (readLine(prompt, line)) {
        try {
            std::size_t used = 0;
            const int value = std::stoi(line, &used);
            if (used == line.size() && value >= low && value <= high) { out = value; return true; }
        } catch (const std::exception&) {
        }
        std::cout << "Please enter a whole number from " << low << " to " << high << ".\n";
    }
    return false;
}

bool readMoney(const std::string& prompt, long long& out) {
    std::string line;
    while (readLine(prompt, line)) {
        if (parseMoney(line, &out) && out >= 1 && out <= Inventory::kMaxPricePaise) return true;
        std::cout << "Enter an amount like 49 or 49.50 (up to 1000000).\n";
    }
    return false;
}

// An empty answer means "no date". Keeps asking until the answer is a real date or empty.
bool readOptionalDate(const std::string& prompt, std::optional<Date>& out) {
    std::string line;
    while (readLine(prompt, line)) {
        out.reset();
        if (line.find_first_not_of(" \t") == std::string::npos) return true;
        Date d;
        if (parseDate(line, &d)) { out = d; return true; }
        std::cout << "Use the format YYYY-MM-DD (a real date), or leave it empty.\n";
    }
    return false;
}

void printProducts(const std::vector<const Product*>& products, const Date& now) {
    if (products.empty()) { std::cout << "  (none)\n"; return; }
    std::size_t skuWidth = 5;  // wide enough for the longest SKU plus two spaces
    for (const Product* p : products) skuWidth = std::max(skuWidth, p->sku.size() + 2);
    std::cout << "\n" << std::left << std::setw(static_cast<int>(skuWidth)) << "SKU" << std::setw(26) << "Name" << std::setw(12) << "Category"
              << std::right << std::setw(9) << "Price" << std::setw(7) << "Qty" << std::setw(9) << "Reorder"
              << "  " << std::left << std::setw(11) << "Expiry" << "Note\n";
    for (const Product* p : products) {
        std::string note;
        if (p->quantity <= p->reorderLevel) note = "LOW";
        if (p->expiry) {
            const long long left = daysBetween(now, *p->expiry);
            if (left < 0) note += std::string(note.empty() ? "" : ", ") + "EXPIRED";
            else if (left <= kSoonDays) note += std::string(note.empty() ? "" : ", ") + "expires in " + std::to_string(left) + "d";
        }
        std::cout << std::left << std::setw(static_cast<int>(skuWidth)) << p->sku << std::setw(26) << p->name.substr(0, 25) << std::setw(12)
                  << p->category.substr(0, 11) << std::right << std::setw(9) << formatMoney(p->pricePaise) << std::setw(7)
                  << p->quantity << std::setw(9) << p->reorderLevel << "  " << std::left << std::setw(11)
                  << (p->expiry ? formatDate(*p->expiry) : "-") << note << "\n";
    }
    std::cout << std::right << products.size() << " product(s)\n";
}

void addProduct(Inventory& inv) {
    Product p;
    std::string line;
    int number = 0;
    if (!readLine("SKU: ", p.sku) || !readLine("Name: ", p.name) || !readLine("Category: ", p.category)) return;
    if (!readMoney("Price (Rs): ", p.pricePaise)) return;
    if (!readInt("Starting quantity: ", 0, Inventory::kMaxQuantity, number)) return;
    p.quantity = number;
    if (!readInt("Reorder level: ", 0, Inventory::kMaxQuantity, number)) return;
    p.reorderLevel = number;
    if (!readOptionalDate("Expiry date YYYY-MM-DD (empty = none): ", p.expiry)) return;
    std::cout << inv.add(p).message << "\n";
}

void receiveStock(Inventory& inv) {
    std::string sku;
    int quantity = 0;
    std::optional<Date> expiry;
    if (!readLine("SKU: ", sku) || !readInt("Quantity received: ", 1, Inventory::kMaxQuantity, quantity)) return;
    if (!readOptionalDate("New expiry date for this stock (empty = unchanged): ", expiry)) return;
    std::cout << inv.receive(sku, quantity, expiry).message << "\n";
}

void sellStock(Inventory& inv) {
    std::string sku;
    int quantity = 0;
    if (!readLine("SKU: ", sku) || !readInt("Quantity sold: ", 1, Inventory::kMaxQuantity, quantity)) return;
    std::cout << inv.issue(sku, quantity).message << "\n";
}

void buyByCategory(Inventory& inv, const Date& now) {
    std::string category;
    int quantity = 0;
    if (!readLine("Category: ", category) || !readInt("Quantity wanted: ", 1, Inventory::kMaxQuantity, quantity)) return;

    const PurchasePlan plan = inv.planPurchase(category, quantity, now);
    if (!plan.ok) { std::cout << plan.message << "\n"; return; }
    std::cout << "\nCheapest way to buy " << quantity << " from '" << category << "':\n";
    for (const PurchaseLine& l : plan.lines) {
        std::cout << "  " << l.quantity << " x " << l.sku << "  " << l.name << "  @ Rs " << formatMoney(l.unitPaise) << "\n";
    }
    std::cout << "  Total: Rs " << formatMoney(plan.totalPaise) << "\n";

    std::string answer;
    if (!readLine("Confirm purchase? (y/n): ", answer) || answer.empty() || (answer[0] != 'y' && answer[0] != 'Y')) {
        std::cout << "Not purchased.\n";
        return;
    }
    std::cout << inv.purchase(category, quantity, now).message << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "data/inventory.csv";
    Inventory inv(path);
    std::string error;
    if (!inv.load(&error)) { std::cerr << "Could not load inventory: " << error << "\n"; return 1; }
    if (inv.skippedLines() > 0) std::cerr << "Warning: skipped " << inv.skippedLines() << " damaged or duplicate line(s) in " << path << "\n";

    const Date now = today();
    std::cout << "Grocery Inventory System: " << inv.all().size() << " product(s) loaded from " << path << " (today is "
              << formatDate(now) << ")\n";
    int choice = -1;
    while (choice != 0) {
        std::cout << "\n1. List products\n2. Add product\n3. Receive stock\n4. Sell stock\n5. Buy by category (cheapest first)\n"
                  << "6. Low-stock report\n7. Expiry report\n8. Search\n9. Change price\n10. Remove product\n"
                  << "11. Inventory value\n0. Exit\n";
        if (!readInt("Choose an option: ", 0, 11, choice)) break;
        std::string text;
        switch (choice) {
            case 1: printProducts(inv.all(), now); break;
            case 2: addProduct(inv); break;
            case 3: receiveStock(inv); break;
            case 4: sellStock(inv); break;
            case 5: buyByCategory(inv, now); break;
            case 6: printProducts(inv.lowStock(), now); break;
            case 7: std::cout << "Expired or expiring within " << kSoonDays << " days:\n";
                    printProducts(inv.expiringWithin(now, kSoonDays), now); break;
            case 8: if (readLine("Search by SKU, name or category: ", text)) printProducts(inv.search(text), now); break;
            case 9: {
                long long price = 0;
                if (readLine("SKU: ", text) && readMoney("New price (Rs): ", price)) std::cout << inv.setPrice(text, price).message << "\n";
                break;
            }
            case 10: if (readLine("SKU to remove: ", text)) std::cout << inv.remove(text).message << "\n"; break;
            case 11: std::cout << "Total stock value: Rs " << formatMoney(inv.totalValuePaise()) << "\n"; break;
            default: break;
        }
    }
    std::cout << "Goodbye.\n";
    return 0;
}
