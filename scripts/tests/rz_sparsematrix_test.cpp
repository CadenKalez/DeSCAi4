// cRZSparseMatrix<T>: cell insert/overwrite, removal (cell, row, column), range counts and dumps,
// min/max used column, min/max used row per column and column per row, row counts per column,
// both iterators, limits and Shutdown. Also checks the quirks: -2147483648 sentinel on an empty
// matrix, int limits default of 0x80000001..0x7fffffff, RemoveCol dropping emptied rows,
// RemoveRow subtracting each row's size, Shutdown only clearing an initialised matrix.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "rz/SparseMatrix.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using Grid = rz::cRZSparseMatrix<int>;
using Cell = rz::MatrixUserCell<int>;

static int MaxInt() { return 2147483647; }
static int MinInt() { return static_cast<int>(0x80000000u); }

int main()
{
    // --- limits -------------------------------------------------------------------------------
    {
        Grid m;
        CHECK(!m.IsInitialised());
        std::int32_t a, b, c, d;
        m.GetLimits(a, b, c, d);
        CHECK(a == -2147483647 && b == 2147483647 && c == -2147483647 && d == 2147483647);   // 0x80000001..0x7fffffff
        CHECK(m.InsertCell(0, 0, 7));       // no limits set: everything is inside the default range
        CHECK(m.GetCellCount() == 1);
        m.SetLimits(0, 10, 0, 10);
        CHECK(m.IsInitialised());
        CHECK(m.InsertCell(10, 10, 1));
        CHECK(!m.InsertCell(11, 5, 1));     // column past the limit
        CHECK(!m.InsertCell(5, 11, 1));     // row past the limit
        CHECK(!m.InsertCell(-1, 5, 1));
        CHECK(!m.InsertCell(5, -1, 1));
        CHECK(m.InsertCell(0, 0, 2));       // inside
    }

    // --- insert / overwrite / count -----------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        int previous = -1;
        CHECK(m.InsertCell(1, 2, 100));
        CHECK(m.GetCellCount() == 1);
        CHECK(m.InsertCell(1, 2, 200, &previous));
        CHECK(previous == 100);
        CHECK(m.GetCellCount() == 1);       // overwrite does not count twice
        int* p = nullptr;
        CHECK(m.GetCellPtr(1, 2, &p) && p && *p == 200);
        int* missing = nullptr;
        CHECK(!m.GetCellPtr(1, 3, &missing));
        CHECK(!m.GetCellPtr(3, 2, &missing));
        CHECK(m.GetCellPtr(1, 2, nullptr)); // null out pointer is allowed
        CHECK(m.GetRowCount() == 1);
    }

    // --- cell removal -------------------------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        m.InsertCell(0, 0, 1);
        m.InsertCell(1, 0, 2);
        m.InsertCell(0, 1, 3);
        CHECK(m.GetCellCount() == 3);
        int removed = 0;
        CHECK(m.RemoveCell(0, 1, &removed));
        CHECK(removed == 3);
        CHECK(m.GetCellCount() == 2);
        CHECK(!m.RemoveCell(0, 1, &removed));   // gone
        CHECK(m.RemoveCell(0, 0, &removed));
        CHECK(removed == 1);
        CHECK(m.RemoveCell(1, 0, &removed));    // last cell of row 0 drops the row
        CHECK(m.GetRowCount() == 0);
        CHECK(m.GetCellCount() == 0);
        CHECK(m.RemoveCell(0, 0, nullptr) == false);
    }

    // --- column removal (0x1002573d0): per-cell count, emptied rows dropped ---------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) m.InsertCell(col, row, row * 10 + col);
        CHECK(m.GetCellCount() == 9);
        CHECK(m.RemoveCol(1, 2));               // columns 1 and 2
        CHECK(m.GetCellCount() == 3);
        CHECK(m.GetRowCount() == 3);
        int* v = nullptr;
        CHECK(m.GetCellPtr(0, 2, &v) && v && *v == 20);
        CHECK(m.RemoveCol(0, 1));               // every row empties and goes away
        CHECK(m.GetRowCount() == 0);
        CHECK(m.GetCellCount() == 0);
        CHECK(m.RemoveCol(0, 0));               // a zero count is a no-op answering true
    }
    {
        Grid m;                                  // RemoveCol over a row that has no cell in the range
        m.SetLimits(0, 9, 0, 9);
        m.InsertCell(5, 1, 1);
        CHECK(m.RemoveCol(0, 3));
        CHECK(m.GetCellCount() == 1 && m.GetRowCount() == 1);
    }

    // --- row removal (0x10025796e): subtract each row's size ------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        m.InsertCell(0, 0, 1);
        m.InsertCell(1, 0, 2);
        m.InsertCell(0, 1, 3);
        m.InsertCell(0, 2, 4);
        CHECK(m.GetCellCount() == 4);
        CHECK(m.RemoveRow(0, 2));               // rows 0 and 1 (3 cells)
        CHECK(m.GetCellCount() == 1);
        CHECK(m.GetRowCount() == 1);
        CHECK(m.RemoveRow(0, 5));               // rows without cells are skipped
        CHECK(m.GetCellCount() == 0);
        CHECK(m.RemoveRow(3, 0));
    }

    // --- range count and dump -------------------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) m.InsertCell(col, row, row * 10 + col);
        CHECK(m.GetCellCountForRange(1, 2, 1, 2) == 4);
        CHECK(m.GetCellCountForRange(0, 3, 0, 3) == 16);
        CHECK(m.GetCellCountForRange(5, 9, 0, 3) == 0);
        CHECK(m.GetCellCountForRange(0, 3, 5, 9) == 0);
        CHECK(m.GetCellCountForRange(2, 1, 0, 3) == 0);   // inverted column range
        const int n = m.GetCellCountForRange(1, 3, 0, 1);
        CHECK(n == 6);
        std::vector<Cell> cells(static_cast<std::size_t>(n));
        m.GetCellRange(1, 3, 0, 1, cells.data());
        CHECK(cells[0].col == 1 && cells[0].row == 0 && cells[0].value == 1);    // row-major, column-minor
        CHECK(cells[1].col == 2 && cells[1].row == 0 && cells[1].value == 2);
        CHECK(cells[2].col == 3 && cells[2].row == 0 && cells[2].value == 3);
        CHECK(cells[5].col == 3 && cells[5].row == 1 && cells[5].value == 13);
        m.GetCellRange(0, 3, 0, 3, nullptr);   // a null dump only walks
    }

    // --- min / max used column ------------------------------------------------------------------
    {
        Grid m;
        std::int32_t out = 0;
        m.GetMinUsedCol(out);
        CHECK(out == MinInt());                 // 0x100256296 keeps 0x80000000 on an empty matrix
        m.GetMaxUsedCol(out);
        CHECK(out == MinInt());
        m.InsertCell(4, 1, 0);
        m.InsertCell(7, 1, 0);
        m.InsertCell(2, 5, 0);
        m.GetMinUsedCol(out);
        CHECK(out == 2);
        m.GetMaxUsedCol(out);
        CHECK(out == 7);
        m.RemoveCell(2, 5, nullptr);
        m.GetMinUsedCol(out);
        CHECK(out == 4);
        m.GetMaxUsedCol(out);
        CHECK(out == 7);
    }

    // --- min/max used row for a column -----------------------------------------------------------
    {
        Grid m;
        std::int32_t lo = 0, hi = 0;
        CHECK(!m.GetMinMaxUsedRowForCol(3, lo, hi));
        CHECK(lo == MaxInt() && hi == MinInt());    // untouched outputs, as the original leaves them
        m.InsertCell(3, 5, 0);
        m.InsertCell(3, 9, 0);
        m.InsertCell(3, 7, 0);
        m.InsertCell(4, 1, 0);
        CHECK(m.GetMinMaxUsedRowForCol(3, lo, hi));
        CHECK(lo == 5 && hi == 9);
        CHECK(m.GetMinMaxUsedRowForCol(4, lo, hi));   // column 4 only has row 1
        CHECK(lo == 1 && hi == 1);
        m.RemoveCell(3, 9, nullptr);
        CHECK(m.GetMinMaxUsedRowForCol(3, lo, hi));
        CHECK(lo == 5 && hi == 7);
    }

    // --- min/max used column for a row -----------------------------------------------------------
    {
        Grid m;
        std::int32_t lo = 0, hi = 0;
        CHECK(!m.GetMinMaxUsedColForRow(2, lo, hi));
        CHECK(lo == MaxInt() && hi == MinInt());
        m.InsertCell(6, 2, 0);
        m.InsertCell(1, 2, 0);
        m.InsertCell(4, 2, 0);
        CHECK(m.GetMinMaxUsedColForRow(2, lo, hi));
        CHECK(lo == 1 && hi == 6);
        m.RemoveCol(1, 2);
        CHECK(m.GetMinMaxUsedColForRow(2, lo, hi));
        CHECK(lo == 4 && hi == 6);
    }

    // --- rows per column -------------------------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        CHECK(m.GetRowCountForCol(2) == 0);
        m.InsertCell(2, 0, 0);
        m.InsertCell(2, 3, 0);
        m.InsertCell(1, 1, 0);
        CHECK(m.GetRowCountForCol(2) == 2);
        CHECK(m.GetRowCountForCol(1) == 1);
        CHECK(m.GetRowCountForCol(0) == 0);
    }

    // --- row-major iterator ----------------------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        m.InsertCell(3, 1, 30);
        m.InsertCell(1, 1, 10);
        m.InsertCell(0, 0, 0);
        std::vector<Cell> seen;
        for (auto it = m.begin(); it != m.end(); ++it) seen.push_back(*it);
        CHECK(seen.size() == 3);
        CHECK(seen[0].row == 0 && seen[0].col == 0 && seen[0].value == 0);
        CHECK(seen[1].row == 1 && seen[1].col == 1 && seen[1].value == 10);
        CHECK(seen[2].row == 1 && seen[2].col == 3 && seen[2].value == 30);
        Grid empty;
        CHECK(empty.begin() == empty.end());
    }

    // --- column iterator -------------------------------------------------------------------------
    {
        Grid m;
        m.SetLimits(0, 9, 0, 9);
        m.InsertCell(2, 0, 20);
        m.InsertCell(2, 4, 24);
        m.InsertCell(1, 2, 12);
        std::vector<Cell> seen;
        for (auto it = m.ColBegin(2); it != m.ColEnd(2); ++it) seen.push_back(*it);
        CHECK(seen.size() == 2);
        CHECK(seen[0].row == 0 && seen[0].col == 2 && seen[0].value == 20);
        CHECK(seen[1].row == 4 && seen[1].col == 2 && seen[1].value == 24);
        CHECK(m.ColBegin(7) == m.ColEnd(7));
    }

    // --- Shutdown (0x10025d776) ------------------------------------------------------------------
    {
        Grid m;
        CHECK(m.Shutdown());                    // not initialised: nothing to do
        m.InsertCell(0, 0, 1);
        CHECK(m.GetCellCount() == 1);           // still there, the original only clears an initialised matrix
        m.SetLimits(0, 4, 0, 4);
        m.InsertCell(1, 1, 2);
        CHECK(m.GetCellCount() == 2);
        CHECK(m.Shutdown());
        CHECK(m.GetCellCount() == 0 && m.GetRowCount() == 0);
        CHECK(!m.IsInitialised());
        std::int32_t lo = 0;
        m.GetMinUsedCol(lo);
        CHECK(lo == MinInt());
    }

    // --- a bigger random walk against a dense reference -------------------------------------------
    {
        const int k = 12;
        Grid m;
        m.SetLimits(0, k - 1, 0, k - 1);
        int dense[k][k];
        for (int r = 0; r < k; ++r)
            for (int c = 0; c < k; ++c) dense[r][c] = 0;
        std::uint32_t seed = 12345;
        auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
        for (int step = 0; step < 4000; ++step) {
            const int r = static_cast<int>(next() % k), c = static_cast<int>(next() % k);
            if (next() & 1) {
                const bool had = dense[r][c] != 0;
                const int value = static_cast<int>(next() % 1000) + 1;
                CHECK(m.InsertCell(c, r, value));
                dense[r][c] = value;                    // overwrite too
                (void)had;
            } else {
                int out = 0;
                const bool had = dense[r][c] != 0;
                CHECK(m.RemoveCell(c, r, &out) == had);
                if (had) CHECK(out == dense[r][c]);
                dense[r][c] = 0;
            }
            if (step % 97 == 0) {
                int total = 0;
                for (int rr = 0; rr < k; ++rr)
                    for (int cc = 0; cc < k; ++cc) total += dense[rr][cc] != 0;
                CHECK(m.GetCellCount() == total);
                int lo = 0, hi = 0;
                m.GetMinMaxUsedColForRow(r, lo, hi);
                if (total == 0) {
                    CHECK(lo == MaxInt() && hi == MinInt());
                } else {
                    int wantLo = -1, wantHi = -1;
                    for (int cc = 0; cc < k; ++cc) {
                        if (!dense[r][cc]) continue;
                        if (wantLo < 0) wantLo = cc;
                        wantHi = cc;
                    }
                    CHECK(wantLo >= 0);
                    CHECK(lo == wantLo && hi == wantHi);
                }
            }
        }
        int denseCount = 0;
        for (int r = 0; r < k; ++r)
            for (int c = 0; c < k; ++c) denseCount += dense[r][c] != 0;
        CHECK(m.GetCellCount() == denseCount);
        int iterated = 0;
        for (auto it = m.begin(); it != m.end(); ++it) {
            const Cell cell = *it;
            CHECK(dense[cell.row][cell.col] == cell.value);
            ++iterated;
        }
        CHECK(iterated == denseCount);
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("rz_sparsematrix: ok\n");
    return 0;
}
