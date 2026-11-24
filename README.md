# Grocery Inventory System (C++17)

A console app for managing a grocery store's stock: add products, receive and sell stock, get low-stock and expiry reports, and buy from a category **cheapest first**. Everything is saved to a CSV file after every change.

All code was written from scratch for this project. The sample data (`data/sample_inventory.csv`) is fictional.

## Features

- **Products** with SKU, name, category, price, quantity, reorder level and an optional expiry date. Add, remove, change price, search by SKU, name or category.
- **Receive and sell stock** with checks: no selling more than is in stock, no negative or zero quantities, and a cap on total stock.
- **Buy by category, cheapest first** (a greedy algorithm): ask for N units of a category and it fills the order from the lowest-priced products first. Ties go to the earliest expiry date, and expired or out-of-stock products are skipped. It is all or nothing: if the category cannot supply the full amount, nothing is bought.
- **Reports:** low stock (quantity at or below the reorder level), items expired or expiring within 7 days, and total stock value.
- **Safe saving:** each change is written to a temporary file and renamed into place. If saving fails, the change is undone completely, so memory and file never disagree.
- **Exact money:** prices are stored in integer paise, so there are no floating-point rounding errors.
- **Strict input checks:** real calendar dates (leap years handled), money like `49` or `49.50`, and damaged lines in the data file are skipped and reported.

## Build and run

Requires a C++17 compiler and `make`. Run from the project folder.

```bash
make demo                          # runs on a copy of the sample data
make && ./grocery-inventory        # uses data/inventory.csv (created on first change)
./grocery-inventory my-stock.csv   # or any other file
```

## Example

Buying 10 units from the `rice` category when two brands are in stock:

```
Cheapest way to buy 10 from 'rice':
  10 x RICE-1KG-B  Basmati Rice 1kg (Brand B)  @ Rs 105.50
  Total: Rs 1055.00
```

## Tests

```bash
make test
```

100 checks covering dates and money parsing, validation, stock movements and their limits, both reports (boundary days included), the cheapest-first purchase rules, saving and loading, damaged files, and a failed save. A randomized test also runs 600 random operations against a simple model and checks after every step that the inventory equals the model and equals what a fresh load from the file gives. The tests also pass with `-fsanitize=address,undefined`.

## Project structure

```
include/Inventory.h      Product, dates, money, the Inventory class
src/Inventory.cpp        Rules, queries, purchase planning, saving and loading
src/main.cpp             Menu
tests/test_main.cpp      Unit tests and the randomized model test
data/sample_inventory.csv  Fictional sample products
```

## Data file format

`sku,name,category,price_paise,quantity,reorder_level,expiry` with one product per line (expiry is `YYYY-MM-DD` or empty). Lines starting with `#` are comments.

## Limitations

- Each product has a single expiry date. Receiving new stock keeps the old date unless you enter a new one, so batches are not tracked separately.
- There is no sales history or user login, and the file is not locked for use by two programs at once.
- The expiry dates in the sample data are examples and will be in the past after a while.
