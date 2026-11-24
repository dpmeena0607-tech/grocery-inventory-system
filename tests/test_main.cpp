// Unit tests plus a randomised model test. Run from the project folder with `make test`.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>

#include "Inventory.h"

namespace {

int passes = 0, failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++passes; } else { ++failures; std::cerr << "FAILED line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

const char* kPath = "test_inv.csv";
const Date kToday{2026, 11, 23};

void cleanFiles() {
    std::remove(kPath);
    std::remove("test_inv.csv.tmp");
}

Product mk(const std::string& sku, const std::string& name, const std::string& cat, long long price, int qty,
           int reorder = 0, const std::string& expiry = "") {
    Product p;
    p.sku = sku; p.name = name; p.category = cat; p.pricePaise = price; p.quantity = qty; p.reorderLevel = reorder;
    Date d;
    if (!expiry.empty() && parseDate(expiry, &d)) p.expiry = d;
    return p;
}

void testDatesAndMoney() {
    Date d;
    CHECK(parseDate("2026-11-23", &d) && d.year == 2026 && d.month == 11 && d.day == 23);
    CHECK(parseDate("2024-02-29", &d));   // leap year
    CHECK(!parseDate("2026-02-29", &d));  // not a leap year
    CHECK(!parseDate("1900-02-29", &d));  // century, not a leap year
    CHECK(parseDate("2000-02-29", &d));   // divisible by 400
    CHECK(!parseDate("2026-13-01", &d) && !parseDate("2026-00-10", &d) && !parseDate("2026-04-31", &d));
    CHECK(!parseDate("2026-1-1", &d) && !parseDate("26-11-23", &d) && !parseDate("2026/11/23", &d) && !parseDate("", &d));
    CHECK(!parseDate("2026-11-2a", &d) && !parseDate("1899-12-31", &d));
    CHECK(formatDate(Date{2026, 1, 5}) == "2026-01-05");

    CHECK(daysBetween(Date{2026, 11, 23}, Date{2026, 11, 23}) == 0);
    CHECK(daysBetween(Date{2026, 11, 23}, Date{2026, 11, 30}) == 7);
    CHECK(daysBetween(Date{2026, 11, 23}, Date{2026, 11, 20}) == -3);
    CHECK(daysBetween(Date{2026, 12, 31}, Date{2027, 1, 1}) == 1);
    CHECK(daysBetween(Date{2024, 2, 28}, Date{2024, 3, 1}) == 2);   // leap February
    CHECK(daysBetween(Date{2026, 2, 28}, Date{2026, 3, 1}) == 1);
    CHECK(daysBetween(Date{2026, 1, 1}, Date{2027, 1, 1}) == 365);

    long long p = 0;
    CHECK(parseMoney("49", &p) && p == 4900);
    CHECK(parseMoney("49.5", &p) && p == 4950);
    CHECK(parseMoney(" 49.50 ", &p) && p == 4950);
    CHECK(parseMoney("0.05", &p) && p == 5);
    CHECK(!parseMoney("49.505", &p) && !parseMoney("-5", &p) && !parseMoney("4,9", &p) && !parseMoney("", &p));
    CHECK(!parseMoney("abc", &p) && !parseMoney("49.", &p) && !parseMoney(".5", &p) && !parseMoney("1234567890", &p));
    CHECK(formatMoney(4950) == "49.50" && formatMoney(5) == "0.05" && formatMoney(100) == "1.00");
}

void testAddValidationAndSearch() {
    cleanFiles();
    Inventory inv(kPath);
    CHECK(inv.load());
    CHECK(inv.add(mk(" rice-1kg ", "Basmati, \"Rice\"", "Rice", 12000, 10, 5)).ok);
    const Product* p = inv.find("RICE-1KG");
    CHECK(p != nullptr && p->name == "Basmati Rice" && p->category == "rice");  // cleaned and normalised
    CHECK(inv.find(" rice-1kg ") == p);
    CHECK(!inv.add(mk("RICE-1KG", "Dup", "rice", 100, 1)).ok);       // duplicate SKU (case-insensitive)
    CHECK(!inv.add(mk("", "x", "c", 100, 1)).ok && !inv.add(mk("A B", "x", "c", 100, 1)).ok);
    CHECK(!inv.add(mk("A1", "", "c", 100, 1)).ok && !inv.add(mk("A1", "x", "  ", 100, 1)).ok);
    CHECK(!inv.add(mk("A1", "x", "c", 0, 1)).ok && !inv.add(mk("A1", "x", "c", Inventory::kMaxPricePaise + 1, 1)).ok);
    CHECK(!inv.add(mk("A1", "x", "c", 100, -1)).ok && !inv.add(mk("A1", "x", "c", 100, Inventory::kMaxQuantity + 1)).ok);
    CHECK(!inv.add(mk("A1", "x", "c", 100, 1, -1)).ok);
    CHECK(!inv.add(mk("THIS-SKU-IS-FAR-TOO-LONG-TO-BE-OK", "x", "c", 100, 1)).ok);
    CHECK(inv.all().size() == 1);

    CHECK(inv.add(mk("MILK", "Toned Milk", "dairy", 2800, 4, 5, "2026-11-25")).ok);
    CHECK(inv.search("MILK").size() == 1 && inv.search("rice").size() == 1 && inv.search("DAIRY").size() == 1);
    CHECK(inv.search("zzz").empty() && inv.search("").empty() && inv.search("  ").empty());
    CHECK(inv.all()[0]->sku == "MILK" && inv.all()[1]->sku == "RICE-1KG");  // sorted by SKU
    cleanFiles();
}

void testStockMovement() {
    cleanFiles();
    Inventory inv(kPath);
    CHECK(inv.load() && inv.add(mk("A", "Item", "c", 1000, 10, 3)).ok);

    CHECK(inv.issue("a", 4).ok && inv.find("A")->quantity == 6);
    CHECK(!inv.issue("A", 7).ok && inv.find("A")->quantity == 6);          // more than in stock: unchanged
    CHECK(inv.issue("A", 6).ok && inv.find("A")->quantity == 0);           // exactly all of it
    CHECK(!inv.issue("A", 1).ok);
    CHECK(!inv.issue("A", 0).ok && !inv.issue("A", -2).ok && !inv.issue("NOPE", 1).ok);

    CHECK(inv.receive("A", 5).ok && inv.find("A")->quantity == 5);
    CHECK(!inv.receive("A", 0).ok && !inv.receive("A", -1).ok && !inv.receive("NOPE", 1).ok);
    CHECK(!inv.receive("A", Inventory::kMaxQuantity).ok && inv.find("A")->quantity == 5);  // would exceed the cap
    CHECK(inv.receive("A", Inventory::kMaxQuantity - 5).ok && inv.find("A")->quantity == Inventory::kMaxQuantity);

    Date d;
    CHECK(parseDate("2027-01-01", &d));
    CHECK(inv.issue("A", 1).ok && inv.receive("A", 1, d).ok && inv.find("A")->expiry && *inv.find("A")->expiry == d);

    CHECK(inv.setPrice("A", 1250).ok && inv.find("A")->pricePaise == 1250);
    CHECK(!inv.setPrice("A", 0).ok && !inv.setPrice("A", Inventory::kMaxPricePaise + 1).ok && !inv.setPrice("NOPE", 5).ok);
    CHECK(inv.find("A")->pricePaise == 1250);

    CHECK(inv.remove("a").ok && inv.find("A") == nullptr && !inv.remove("A").ok);
    cleanFiles();
}

void testReports() {
    cleanFiles();
    Inventory inv(kPath);
    CHECK(inv.load());
    CHECK(inv.add(mk("LOW1", "Low", "c", 100, 2, 5)).ok);                       // below reorder level
    CHECK(inv.add(mk("EDGE", "Edge", "c", 100, 5, 5)).ok);                      // equal to reorder level counts as low
    CHECK(inv.add(mk("OK", "Fine", "c", 100, 6, 5)).ok);
    CHECK(inv.add(mk("ZERO", "Zero", "c", 100, 0, 0)).ok);                      // 0 <= 0 is low
    auto low = inv.lowStock();
    CHECK(low.size() == 3 && low[0]->sku == "EDGE" && low[1]->sku == "LOW1" && low[2]->sku == "ZERO");

    CHECK(inv.add(mk("EXP-OLD", "Old", "d", 100, 1, 0, "2026-11-20")).ok);      // expired 3 days ago
    CHECK(inv.add(mk("EXP-TODAY", "Today", "d", 100, 1, 0, "2026-11-23")).ok);  // expires today
    CHECK(inv.add(mk("EXP-7", "Seven", "d", 100, 1, 0, "2026-11-30")).ok);      // exactly 7 days away
    CHECK(inv.add(mk("EXP-8", "Eight", "d", 100, 1, 0, "2026-12-01")).ok);      // 8 days away
    auto soon = inv.expiringWithin(kToday, 7);
    CHECK(soon.size() == 3 && soon[0]->sku == "EXP-OLD" && soon[1]->sku == "EXP-TODAY" && soon[2]->sku == "EXP-7");
    CHECK(inv.expiringWithin(kToday, 0).size() == 2);   // expired + today
    CHECK(inv.expiringWithin(kToday, -1).size() == 1);  // only already expired

    CHECK(inv.totalValuePaise() == 100 * (2 + 5 + 6 + 0 + 1 + 1 + 1 + 1));
    cleanFiles();
}

void testPurchase() {
    cleanFiles();
    Inventory inv(kPath);
    CHECK(inv.load());
    CHECK(inv.add(mk("R-A", "Rice A", "rice", 12000, 10)).ok);
    CHECK(inv.add(mk("R-B", "Rice B", "rice", 10500, 4)).ok);                       // cheapest
    CHECK(inv.add(mk("R-C", "Rice C", "rice", 10500, 3, 0, "2027-01-01")).ok);      // same price, has an expiry date
    CHECK(inv.add(mk("R-D", "Rice D", "rice", 9000, 5, 0, "2026-11-22")).ok);       // cheapest but expired
    CHECK(inv.add(mk("R-E", "Rice E", "rice", 8000, 0)).ok);                        // cheapest but out of stock
    CHECK(inv.add(mk("OIL", "Oil", "oil", 16500, 7)).ok);

    // 6 units: R-C (earlier expiry beats "no expiry" at equal price) 3, then R-B 3.
    auto plan = inv.planPurchase("RICE", 6, kToday);
    CHECK(plan.ok && plan.lines.size() == 2 && plan.lines[0].sku == "R-C" && plan.lines[0].quantity == 3);
    CHECK(plan.lines[1].sku == "R-B" && plan.lines[1].quantity == 3 && plan.totalPaise == 6 * 10500);
    CHECK(inv.find("R-B")->quantity == 4);  // planning changes nothing

    // 10 units: 3 (R-C) + 4 (R-B) + 3 (R-A).
    plan = inv.purchase("rice", 10, kToday);
    CHECK(plan.ok && plan.lines.size() == 3 && plan.totalPaise == 7 * 10500 + 3 * 12000);
    CHECK(inv.find("R-C")->quantity == 0 && inv.find("R-B")->quantity == 0 && inv.find("R-A")->quantity == 7);
    CHECK(inv.find("R-D")->quantity == 5);  // the expired one was never touched

    // All or nothing: only 7 sellable units remain.
    plan = inv.purchase("rice", 8, kToday);
    CHECK(!plan.ok && plan.message.find("Only 7") != std::string::npos && inv.find("R-A")->quantity == 7);
    CHECK(!inv.purchase("rice", 0, kToday).ok && !inv.purchase("", 1, kToday).ok && !inv.purchase("bread", 1, kToday).ok);
    CHECK(inv.purchase("rice", 7, kToday).ok && inv.find("R-A")->quantity == 0);
    CHECK(inv.planPurchase("oil", 7, kToday).ok && !inv.planPurchase("oil", 8, kToday).ok);
    cleanFiles();
}

void testPersistence() {
    cleanFiles();
    {
        Inventory inv(kPath);
        CHECK(inv.load());
        CHECK(inv.add(mk("A", "Alpha", "x", 1999, 7, 2, "2027-05-01")).ok);
        CHECK(inv.add(mk("B", "Beta", "y", 500, 0, 0)).ok);
        CHECK(inv.issue("A", 2).ok && inv.setPrice("B", 600).ok);
    }
    Inventory again(kPath);
    CHECK(again.load() && again.skippedLines() == 0 && again.all().size() == 2);
    CHECK(again.find("A")->quantity == 5 && again.find("A")->pricePaise == 1999 && again.find("A")->reorderLevel == 2);
    CHECK(again.find("A")->expiry && formatDate(*again.find("A")->expiry) == "2027-05-01");
    CHECK(again.find("B")->pricePaise == 600 && !again.find("B")->expiry);

    // Damaged, duplicate and out-of-range lines are skipped; good ones survive.
    {
        std::ofstream out(kPath, std::ios::app);
        out << "garbage\n" << "C,Gamma,z,abc,1,1,\n" << "D,Delta,z,100,1,1,2026-02-30\n" << "A,Dup,x,100,1,1,\n"
            << "E,Eps,z,0,1,1,\n" << "F,Phi,z,100,99999999999,1,\n" << "G,Gee,z,100,1,1,2026-01-01,extra\n" << "\n# note\n";
    }
    Inventory damaged(kPath);
    CHECK(damaged.load() && damaged.skippedLines() == 7 && damaged.all().size() == 2);

    // A save that cannot be written changes nothing, in memory or on disk.
    { std::ofstream blocker("test_inv_blocker"); blocker << "x"; }
    Inventory unwritable(std::string("test_inv_blocker/inv.csv"));
    CHECK(unwritable.load());
    CHECK(!unwritable.add(mk("Z", "Zed", "z", 100, 1)).ok && unwritable.all().empty());
    std::remove("test_inv_blocker");
    cleanFiles();
}

// Random operations on the real Inventory and on a plain std::map "model"; after every step the
// inventory must equal the model AND equal what a fresh load from the file gives.
bool same(const Product& a, const Product& b) {
    return a.sku == b.sku && a.name == b.name && a.category == b.category && a.pricePaise == b.pricePaise &&
           a.quantity == b.quantity && a.reorderLevel == b.reorderLevel && a.expiry.has_value() == b.expiry.has_value() &&
           (!a.expiry || *a.expiry == *b.expiry);
}

bool matches(const Inventory& inv, const std::map<std::string, Product>& model) {
    const auto items = inv.all();
    if (items.size() != model.size()) return false;
    std::size_t i = 0;
    for (const auto& entry : model) {
        if (!same(*items[i++], entry.second)) return false;
    }
    return true;
}

void testRandomModel() {
    cleanFiles();
    std::mt19937 rng(20261123);
    const char* cats[] = {"rice", "oil", "milk"};
    Inventory inv(kPath);
    CHECK(inv.load());
    std::map<std::string, Product> model;
    int bad = 0, purchasesOk = 0, purchasesRefused = 0;

    for (int step = 0; step < 600; ++step) {
        const std::string sku = "S" + std::to_string(rng() % 12);
        const int op = static_cast<int>(rng() % 6);
        if (op == 0) {
            Product p = mk(sku, "Item " + sku, cats[rng() % 3], 100 + rng() % 5 * 50, static_cast<int>(rng() % 20),
                           static_cast<int>(rng() % 8));
            if (rng() % 2) p.expiry = Date{2026, 11, 10 + static_cast<int>(rng() % 20)};  // some expired, some not
            const bool expected = model.count(sku) == 0;
            if (inv.add(p).ok != expected) ++bad;
            if (expected) model[sku] = p;
        } else if (op == 1) {
            const int q = static_cast<int>(rng() % 15);
            const bool expected = model.count(sku) != 0 && q >= 1;
            if (inv.receive(sku, q).ok != expected) ++bad;
            if (expected) model[sku].quantity += q;
        } else if (op == 2) {
            const int q = static_cast<int>(rng() % 15);
            const bool expected = model.count(sku) != 0 && q >= 1 && q <= model[sku].quantity;
            if (inv.issue(sku, q).ok != expected) ++bad;
            if (expected) model[sku].quantity -= q;
        } else if (op == 3) {
            const bool expected = model.count(sku) != 0;
            if (inv.remove(sku).ok != expected) ++bad;
            model.erase(sku);
        } else if (op == 4) {
            const long long price = 50 + rng() % 400;
            const bool expected = model.count(sku) != 0;
            if (inv.setPrice(sku, price).ok != expected) ++bad;
            if (expected) model[sku].pricePaise = price;
        } else {
            // Purchase: the model repeatedly takes the cheapest sellable product (price, expiry, sku).
            const std::string cat = cats[rng() % 3];
            const int want = 1 + static_cast<int>(rng() % 25);
            std::map<std::string, Product> next = model;
            int need = want;
            long long total = 0;
            while (need > 0) {
                Product* best = nullptr;
                for (auto& entry : next) {
                    Product& p = entry.second;
                    if (p.category != cat || p.quantity == 0 || (p.expiry && *p.expiry < kToday)) continue;
                    const Date never{9999, 12, 31};
                    if (best == nullptr ||
                        std::make_tuple(p.pricePaise, p.expiry.value_or(never), p.sku) <
                            std::make_tuple(best->pricePaise, best->expiry.value_or(never), best->sku)) {
                        best = &p;
                    }
                }
                if (best == nullptr) break;
                const int take = std::min(need, best->quantity);
                best->quantity -= take;
                total += take * best->pricePaise;
                need -= take;
            }
            const PurchasePlan plan = inv.purchase(cat, want, kToday);
            if (plan.ok != (need == 0)) ++bad;
            if (need == 0) { model = next; ++purchasesOk; if (plan.totalPaise != total) ++bad; } else { ++purchasesRefused; }
        }

        if (!matches(inv, model)) ++bad;
        Inventory reloaded(kPath);
        if (!reloaded.load() || reloaded.skippedLines() != 0 || !matches(reloaded, model)) ++bad;
        if (bad != 0) { std::cerr << "random model test diverged at step " << step << "\n"; break; }
    }
    CHECK(bad == 0);
    CHECK(purchasesOk > 5 && purchasesRefused > 5);  // both outcomes were really exercised
    cleanFiles();
}

}  // namespace

int main() {
    testDatesAndMoney();
    testAddValidationAndSearch();
    testStockMovement();
    testReports();
    testPurchase();
    testPersistence();
    testRandomModel();
    std::cout << passes << " checks passed, " << failures << " failed.\n";
    return failures == 0 ? 0 : 1;
}
