#include <random>
#include "../simulators/gate_data.h"
#include "../simulators/vector_simulator.h"
#include "../test_util.test.h"
#include <gtest/gtest.h>
#include "tableau.h"
#include "tableau_transposed_raii.h"

static float complex_distance(std::complex<float> a, std::complex<float> b) {
    auto d = a - b;
    return sqrtf(d.real()*d.real() + d.imag()*d.imag());
}

TEST(tableau, identity) {
    auto t = Tableau::identity(4);
    ASSERT_EQ(t.str(), "+-xz-xz-xz-xz-\n"
                       "| ++ ++ ++ ++\n"
                       "| XZ __ __ __\n"
                       "| __ XZ __ __\n"
                       "| __ __ XZ __\n"
                       "| __ __ __ XZ");
}

TEST(tableau, gate1) {
    auto gate1 = Tableau::gate1("+X", "+Z");
    ASSERT_EQ(gate1.xs[0].str(), "+X");
    ASSERT_EQ(gate1.eval_y_obs(0).str(), "+Y");
    ASSERT_EQ(gate1.zs[0].str(), "+Z");
}

bool tableau_agrees_with_unitary(const Tableau &tableau,
                                 const std::vector<std::vector<std::complex<float>>> &unitary) {
    auto n = tableau.num_qubits;
    assert(unitary.size() == 1ULL << n);

    std::vector<PauliString> basis;
    for (size_t x = 0; x < 2; x++) {
        for (size_t k = 0; k < n; k++) {
            basis.emplace_back(n);
            if (x) {
                basis.back().xs[k] ^= 1;
            } else {
                basis.back().zs[k] ^= 1;
            }
        }
    }

    for (const auto &input_side_obs : basis) {
        VectorSimulator sim(n * 2);
        // Create EPR pairs to test all possible inputs via state channel duality.
        for (size_t q = 0; q < n; q++) {
            sim.apply("H", q);
            sim.apply("CNOT", q, q + n);
        }
        // Apply input-side observable.
        sim.apply(input_side_obs, n);
        // Apply operation's unitary.
        std::vector<size_t> qs;
        for (size_t q = 0; q < n; q++) {
            qs.push_back(q + n);
        }
        sim.apply(unitary, {qs});
        // Apply output-side observable, which should cancel input-side.
        sim.apply(tableau(input_side_obs), n);

        // Verify that the state encodes the unitary matrix, with the
        // input-side and output-side observables having perfectly cancelled out.
        auto scale = powf(0.5f, 0.5f * n);
        for (size_t row = 0; row < 1u << n; row++) {
            for (size_t col = 0; col < 1u << n; col++) {
                auto a = sim.state[(row << n) | col];
                auto b = unitary[row][col] * scale;
                if (complex_distance(a, b) > 1e-4) {
                    return false;
                }
            }
        }
    }

    return true;
}

TEST(tableau, big_not_seeing_double) {
    Tableau t(500);
    auto s = t.xs[0].str();
    size_t n = 0;
    for (size_t k = 1; k < s.size(); k++) {
        if (s[k] != '_') {
            n += 1;
        }
    }
    ASSERT_EQ(n, 1) << s;
}

TEST(tableau, str) {
    ASSERT_EQ(Tableau::gate1("+X", "-Z").str(),
            "+-xz-\n"
            "| +-\n"
            "| XZ");
    ASSERT_EQ(GATE_TABLEAUS.at("X").str(),
            "+-xz-\n"
            "| +-\n"
            "| XZ");
    ASSERT_EQ(GATE_TABLEAUS.at("S").str(),
            "+-xz-\n"
            "| ++\n"
            "| YZ");
    ASSERT_EQ(GATE_TABLEAUS.at("S_DAG").str(),
            "+-xz-\n"
            "| -+\n"
            "| YZ");
    ASSERT_EQ(GATE_TABLEAUS.at("H").str(),
            "+-xz-\n"
            "| ++\n"
            "| ZX");
    ASSERT_EQ(GATE_TABLEAUS.at("CNOT").str(),
            "+-xz-xz-\n"
            "| ++ ++\n"
            "| XZ _Z\n"
            "| X_ XZ");

    Tableau t(4);
    t.prepend_H_XZ(0);
    t.prepend_H_XZ(1);
    t.prepend_SQRT_Z(1);
    t.prepend_CX(0, 2);
    t.prepend_CX(1, 3);
    t.prepend_CX(0, 1);
    t.prepend_CX(1, 0);
    ASSERT_EQ(t.inverse().str(),
            "+-xz-xz-xz-xz-\n"
            "| ++ +- ++ ++\n"
            "| Z_ ZY _Z _Z\n"
            "| ZX _X _Z __\n"
            "| _X __ XZ __\n"
            "| __ _X __ XZ");
    ASSERT_EQ(t.str(),
            "+-xz-xz-xz-xz-\n"
            "| -+ ++ ++ ++\n"
            "| Z_ ZX _X __\n"
            "| YX _X __ _X\n"
            "| X_ X_ XZ __\n"
            "| X_ __ __ XZ");
}

TEST(tableau, gate_tableau_data_vs_unitary_data) {
    for (const auto &kv : GATE_TABLEAUS) {
        const auto &name = kv.first;
        const auto &tab = kv.second;
        const auto &u = GATE_UNITARIES.at(name);
        ASSERT_TRUE(tableau_agrees_with_unitary(tab, u)) << name;
    }
}

TEST(tableau, inverse_data) {
    for (const auto &kv : GATE_TABLEAUS) {
        const auto &name = kv.first;
        Tableau tab = kv.second;
        const auto &inverse_name = GATE_INVERSE_NAMES.at(name);
        const auto &tab2 = GATE_TABLEAUS.at(inverse_name);
        std::vector<size_t> targets {0};
        while (targets.size() < tab.num_qubits) {
            targets.push_back(targets.size());
        }
        tab.inplace_scatter_append(tab2, targets);
        ASSERT_EQ(tab, Tableau::identity(tab.num_qubits)) << name << " -> " << inverse_name;
    }
}

TEST(tableau, eval) {
    const auto &cnot = GATE_TABLEAUS.at("CNOT");
    ASSERT_EQ(cnot(PauliString::from_str("-XX")), PauliString::from_str("-XI"));
    ASSERT_EQ(cnot(PauliString::from_str("+XX")), PauliString::from_str("+XI"));
    ASSERT_EQ(cnot(PauliString::from_str("+ZZ")), PauliString::from_str("+IZ"));
    ASSERT_EQ(cnot(PauliString::from_str("+IY")), PauliString::from_str("+ZY"));
    ASSERT_EQ(cnot(PauliString::from_str("+YI")), PauliString::from_str("+YX"));
    ASSERT_EQ(cnot(PauliString::from_str("+YY")), PauliString::from_str("-XZ"));

    const auto &x2 = GATE_TABLEAUS.at("SQRT_X");
    ASSERT_EQ(x2(PauliString::from_str("+X")), PauliString::from_str("+X"));
    ASSERT_EQ(x2(PauliString::from_str("+Y")), PauliString::from_str("+Z"));
    ASSERT_EQ(x2(PauliString::from_str("+Z")), PauliString::from_str("-Y"));

    const auto &s = GATE_TABLEAUS.at("S");
    ASSERT_EQ(s(PauliString::from_str("+X")), PauliString::from_str("+Y"));
    ASSERT_EQ(s(PauliString::from_str("+Y")), PauliString::from_str("-X"));
    ASSERT_EQ(s(PauliString::from_str("+Z")), PauliString::from_str("+Z"));
}

TEST(tableau, apply_within) {
    const auto &cnot = GATE_TABLEAUS.at("CNOT");

    auto p1 = PauliString::from_str("-XX");
    PauliStringRef p1_ptr(p1);
    cnot.apply_within(p1_ptr, {0, 1});
    ASSERT_EQ(p1, PauliString::from_str("-XI"));

    auto p2 = PauliString::from_str("+XX");
    PauliStringRef p2_ptr(p2);
    cnot.apply_within(p2_ptr, {0, 1});
    ASSERT_EQ(p2, PauliString::from_str("+XI"));
}

TEST(tableau, equality) {
    ASSERT_TRUE(GATE_TABLEAUS.at("S") == GATE_TABLEAUS.at("SQRT_Z"));
    ASSERT_FALSE(GATE_TABLEAUS.at("S") != GATE_TABLEAUS.at("SQRT_Z"));
    ASSERT_FALSE(GATE_TABLEAUS.at("S") == GATE_TABLEAUS.at("CNOT"));
    ASSERT_TRUE(GATE_TABLEAUS.at("S") != GATE_TABLEAUS.at("CNOT"));
    ASSERT_FALSE(GATE_TABLEAUS.at("S") == GATE_TABLEAUS.at("SQRT_X"));
    ASSERT_TRUE(GATE_TABLEAUS.at("S") != GATE_TABLEAUS.at("SQRT_X"));
    ASSERT_EQ(Tableau(1), GATE_TABLEAUS.at("I"));
    ASSERT_NE(Tableau(1), GATE_TABLEAUS.at("X"));
}

TEST(tableau, inplace_scatter_append) {
    auto t1 = Tableau::identity(1);
    t1.inplace_scatter_append(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("S"));
    t1.inplace_scatter_append(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("Z"));
    t1.inplace_scatter_append(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("S_DAG"));

    // Test swap decomposition into exp(i pi/2 (XX + YY + ZZ)).
    auto t2 = Tableau::identity(2);
    // ZZ^0.5
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_Z"), {0});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_Z"), {1});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("CZ"), {0, 1});
    // YY^0.5
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_Y"), {0});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_Y"), {1});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("H_YZ"), {0});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("CY"), {0, 1});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("H_YZ"), {0});
    // XX^0.5
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_X"), {0});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_X"), {1});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("H"), {0});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("CX"), {0, 1});
    t2.inplace_scatter_append(GATE_TABLEAUS.at("H"), {0});
    ASSERT_EQ(t2, GATE_TABLEAUS.at("SWAP"));

    // Test order dependence.
    auto t3 = Tableau::identity(2);
    t3.inplace_scatter_append(GATE_TABLEAUS.at("H"), {0});
    t3.inplace_scatter_append(GATE_TABLEAUS.at("SQRT_X"), {1});
    t3.inplace_scatter_append(GATE_TABLEAUS.at("CNOT"), {0, 1});
    ASSERT_EQ(t3(PauliString::from_str("XI")), PauliString::from_str("ZI"));
    ASSERT_EQ(t3(PauliString::from_str("ZI")), PauliString::from_str("XX"));
    ASSERT_EQ(t3(PauliString::from_str("IX")), PauliString::from_str("IX"));
    ASSERT_EQ(t3(PauliString::from_str("IZ")), PauliString::from_str("-ZY"));
}

TEST(tableau, inplace_scatter_prepend) {
    auto t1 = Tableau::identity(1);
    t1.inplace_scatter_prepend(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("S"));
    t1.inplace_scatter_prepend(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("Z"));
    t1.inplace_scatter_prepend(GATE_TABLEAUS.at("S"), {0});
    ASSERT_EQ(t1, GATE_TABLEAUS.at("S_DAG"));

    // Test swap decomposition into exp(i pi/2 (XX + YY + ZZ)).
    auto t2 = Tableau::identity(2);
    // ZZ^0.5
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Z"), {0});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Z"), {1});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("CZ"), {0, 1});
    // YY^0.5
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Y"), {0});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Y"), {1});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("H_YZ"), {0});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("CY"), {0, 1});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("H_YZ"), {0});
    // XX^0.5
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_X"), {0});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_X"), {1});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("H"), {0});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("CX"), {0, 1});
    t2.inplace_scatter_prepend(GATE_TABLEAUS.at("H"), {0});
    ASSERT_EQ(t2, GATE_TABLEAUS.at("SWAP"));

    // Test order dependence.
    auto t3 = Tableau::identity(2);
    t3.inplace_scatter_prepend(GATE_TABLEAUS.at("H"), {0});
    t3.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_X"), {1});
    t3.inplace_scatter_prepend(GATE_TABLEAUS.at("CNOT"), {0, 1});
    ASSERT_EQ(t3(PauliString::from_str("XI")), PauliString::from_str("ZX"));
    ASSERT_EQ(t3(PauliString::from_str("ZI")), PauliString::from_str("XI"));
    ASSERT_EQ(t3(PauliString::from_str("IX")), PauliString::from_str("IX"));
    ASSERT_EQ(t3(PauliString::from_str("IZ")), PauliString::from_str("-XY"));
}

TEST(tableau, eval_y) {
    ASSERT_EQ(GATE_TABLEAUS.at("H").zs[0], PauliString::from_str("+X"));
    ASSERT_EQ(GATE_TABLEAUS.at("S").zs[0], PauliString::from_str("+Z"));
    ASSERT_EQ(GATE_TABLEAUS.at("H_YZ").zs[0], PauliString::from_str("+Y"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_Y").zs[0], PauliString::from_str("X"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_Y_DAG").zs[0], PauliString::from_str("-X"));
    ASSERT_EQ(GATE_TABLEAUS.at("CNOT").zs[1], PauliString::from_str("ZZ"));

    ASSERT_EQ(GATE_TABLEAUS.at("H").eval_y_obs(0), PauliString::from_str("-Y"));
    ASSERT_EQ(GATE_TABLEAUS.at("S").eval_y_obs(0), PauliString::from_str("-X"));
    ASSERT_EQ(GATE_TABLEAUS.at("H_YZ").eval_y_obs(0), PauliString::from_str("+Z"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_Y").eval_y_obs(0), PauliString::from_str("+Y"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_Y_DAG").eval_y_obs(0), PauliString::from_str("+Y"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_X").eval_y_obs(0), PauliString::from_str("+Z"));
    ASSERT_EQ(GATE_TABLEAUS.at("SQRT_X_DAG").eval_y_obs(0), PauliString::from_str("-Z"));
    ASSERT_EQ(GATE_TABLEAUS.at("CNOT").eval_y_obs(1), PauliString::from_str("ZY"));
}

bool are_tableau_mutations_equivalent(
        size_t n,
        const std::function<void(Tableau &t, const std::vector<size_t> &)> &mutation1,
        const std::function<void(Tableau &t, const std::vector<size_t> &)> &mutation2) {
    auto test_tableau_dual = Tableau::identity(2 * n);
    std::vector<size_t> targets1;
    std::vector<size_t> targets2;
    std::vector<size_t> targets3;
    for (size_t k = 0; k < n; k++) {
        test_tableau_dual.inplace_scatter_append(GATE_TABLEAUS.at("H"), {k});
        test_tableau_dual.inplace_scatter_append(GATE_TABLEAUS.at("CNOT"), {k, k + n});
        targets1.push_back(k);
        targets2.push_back(k + n);
        targets3.push_back(k + (k % 2 == 0 ? 0 : n));
    }

    std::vector<Tableau> tableaus{
        test_tableau_dual,
        Tableau::random(n + 10, SHARED_TEST_RNG()),
        Tableau::random(n + 30, SHARED_TEST_RNG()),
    };
    std::vector<std::vector<size_t>> cases{targets1, targets2, targets3};
    for (const auto &t : tableaus) {
        for (const auto &targets : cases) {
            auto t1 = t;
            auto t2 = t;
            mutation1(t1, targets);
            mutation2(t2, targets);
            if (t1 != t2) {
                return false;
            }
        }
    }
    return true;
}

bool are_tableau_prepends_equivalent(
        const std::string &name,
        const std::function<void(Tableau &t, size_t)> &func) {
    return are_tableau_mutations_equivalent(
        1,
        [&](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at(name), targets); },
        [&](Tableau &t, const std::vector<size_t> &targets){ func(t, targets[0]); }
    );
}

bool are_tableau_prepends_equivalent(
        const std::string &name,
        const std::function<void(Tableau &t, size_t, size_t)> &func) {
    return are_tableau_mutations_equivalent(
        2,
        [&](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at(name), targets); },
        [&](Tableau &t, const std::vector<size_t> &targets){ func(t, targets[0], targets[1]); }
    );
}

TEST(tableau, check_invariants) {
    ASSERT_TRUE(Tableau::gate1("X", "Z").satisfies_invariants());
    ASSERT_TRUE(Tableau::gate2("XI", "ZI", "IX", "IZ").satisfies_invariants());
    ASSERT_FALSE(Tableau::gate1("X", "X").satisfies_invariants());
    ASSERT_FALSE(Tableau::gate2("XI", "ZI", "XI", "ZI").satisfies_invariants());
    ASSERT_FALSE(Tableau::gate2("XI", "II", "IX", "IZ").satisfies_invariants());
}

TEST(tableau, random) {
    for (size_t k = 0; k < 20; k++) {
        auto t = Tableau::random(1, SHARED_TEST_RNG());
        ASSERT_TRUE(t.satisfies_invariants()) << t;
    }
    for (size_t k = 0; k < 20; k++) {
        auto t = Tableau::random(2, SHARED_TEST_RNG());
        ASSERT_TRUE(t.satisfies_invariants()) << t;
    }
    for (size_t k = 0; k < 20; k++) {
        auto t = Tableau::random(3, SHARED_TEST_RNG());
        ASSERT_TRUE(t.satisfies_invariants()) << t;
    }
    for (size_t k = 0; k < 20; k++) {
        auto t = Tableau::random(30, SHARED_TEST_RNG());
        ASSERT_TRUE(t.satisfies_invariants());
    }
}

TEST(tableau, specialized_operations) {
    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("X"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("X"), targets); }
    ));
    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("S"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Z"), targets); }
    ));
    EXPECT_TRUE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("CNOT"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("CX"), targets); }
    ));
    EXPECT_FALSE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("H"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("SQRT_Y"), targets); }
    ));
    EXPECT_FALSE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("CNOT"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_prepend(GATE_TABLEAUS.at("CZ"), targets); }
    ));

    EXPECT_TRUE(are_tableau_prepends_equivalent("H", &Tableau::prepend_H_XZ));
    EXPECT_TRUE(are_tableau_prepends_equivalent("H_YZ", &Tableau::prepend_H_YZ));
    EXPECT_TRUE(are_tableau_prepends_equivalent("H_XY", &Tableau::prepend_H_XY));
    EXPECT_TRUE(are_tableau_prepends_equivalent("X", &Tableau::prepend_X));
    EXPECT_TRUE(are_tableau_prepends_equivalent("Y", &Tableau::prepend_Y));
    EXPECT_TRUE(are_tableau_prepends_equivalent("Z", &Tableau::prepend_Z));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_X", &Tableau::prepend_SQRT_X));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_Y", &Tableau::prepend_SQRT_Y));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_Z", &Tableau::prepend_SQRT_Z));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_X_DAG", &Tableau::prepend_SQRT_X_DAG));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_Y_DAG", &Tableau::prepend_SQRT_Y_DAG));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SQRT_Z_DAG", &Tableau::prepend_SQRT_Z_DAG));
    EXPECT_TRUE(are_tableau_prepends_equivalent("SWAP", &Tableau::prepend_SWAP));
    EXPECT_TRUE(are_tableau_prepends_equivalent("CX", &Tableau::prepend_CX));
    EXPECT_TRUE(are_tableau_prepends_equivalent("CY", &Tableau::prepend_CY));
    EXPECT_TRUE(are_tableau_prepends_equivalent("CZ", &Tableau::prepend_CZ));
    EXPECT_TRUE(are_tableau_prepends_equivalent("ISWAP", &Tableau::prepend_ISWAP));
    EXPECT_TRUE(are_tableau_prepends_equivalent("ISWAP_DAG", &Tableau::prepend_ISWAP_DAG));
    EXPECT_TRUE(are_tableau_prepends_equivalent("XCX", &Tableau::prepend_XCX));
    EXPECT_TRUE(are_tableau_prepends_equivalent("XCY", &Tableau::prepend_XCY));
    EXPECT_TRUE(are_tableau_prepends_equivalent("XCZ", &Tableau::prepend_XCZ));
    EXPECT_TRUE(are_tableau_prepends_equivalent("YCX", &Tableau::prepend_YCX));
    EXPECT_TRUE(are_tableau_prepends_equivalent("YCY", &Tableau::prepend_YCY));
    EXPECT_TRUE(are_tableau_prepends_equivalent("YCZ", &Tableau::prepend_YCZ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("X"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_X(targets[0]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("H"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_H_XZ(targets[0]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("H_XY"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_H_XY(targets[0]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        1,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("H_YZ"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_H_YZ(targets[0]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("CX"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_CX(targets[0], targets[1]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("CY"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_CY(targets[0], targets[1]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("CZ"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_CZ(targets[0], targets[1]); }
    ));

    EXPECT_TRUE(are_tableau_mutations_equivalent(
        2,
        [](Tableau &t, const std::vector<size_t> &targets){ t.inplace_scatter_append(GATE_TABLEAUS.at("SWAP"), targets); },
        [](Tableau &t, const std::vector<size_t> &targets){ TableauTransposedRaii(t).append_SWAP(targets[0], targets[1]); }
    ));
}

TEST(tableau, expand) {
    auto t = Tableau::random(4, SHARED_TEST_RNG());
    auto t2 = t;
    for (size_t n = 8; n < 500; n += 255) {
        t2.expand(n);
        assert(t2.num_qubits == n);
        for (size_t k = 0; k < n; k++) {
            if (k < 4) {
                ASSERT_EQ(t.xs[k].sign, t2.xs[k].sign);
                ASSERT_EQ(t.zs[k].sign, t2.zs[k].sign);
            } else {
                ASSERT_EQ(t2.xs[k].sign, false);
                ASSERT_EQ(t2.zs[k].sign, false);
            }
            for (size_t k2 = 0; k2 < n; k2++) {
                if (k < 4 && k2 < 4) {
                    ASSERT_EQ(t.xs[k].xs[k2], t2.xs[k].xs[k2]);
                    ASSERT_EQ(t.xs[k].zs[k2], t2.xs[k].zs[k2]);
                    ASSERT_EQ(t.zs[k].xs[k2], t2.zs[k].xs[k2]);
                    ASSERT_EQ(t.zs[k].zs[k2], t2.zs[k].zs[k2]);
                } else if (k == k2) {
                    ASSERT_EQ(t2.xs[k].xs[k2], true);
                    ASSERT_EQ(t2.xs[k].zs[k2], false);
                    ASSERT_EQ(t2.zs[k].xs[k2], false);
                    ASSERT_EQ(t2.zs[k].zs[k2], true);
                } else {
                    ASSERT_EQ(t2.xs[k].xs[k2], false);
                    ASSERT_EQ(t2.xs[k].zs[k2], false);
                    ASSERT_EQ(t2.zs[k].xs[k2], false);
                    ASSERT_EQ(t2.zs[k].zs[k2], false);
                }
            }
        }
    }
}

TEST(tableau, transposed_access) {
    size_t n = 1000;
    Tableau t(n);
    auto m = t.xs.xt.data.num_bits_padded();
    t.xs.xt.data.randomize(m, SHARED_TEST_RNG());
    t.xs.zt.data.randomize(m, SHARED_TEST_RNG());
    t.zs.xt.data.randomize(m, SHARED_TEST_RNG());
    t.zs.zt.data.randomize(m, SHARED_TEST_RNG());
    for (size_t inp_qubit = 0; inp_qubit < 1000; inp_qubit += 99) {
        for (size_t out_qubit = 0; out_qubit < 1000; out_qubit += 99) {
            bool bxx = t.xs.xt[inp_qubit][out_qubit];
            bool bxz = t.xs.zt[inp_qubit][out_qubit];
            bool bzx = t.zs.xt[inp_qubit][out_qubit];
            bool bzz = t.zs.zt[inp_qubit][out_qubit];

            ASSERT_EQ(t.xs[inp_qubit].xs[out_qubit], bxx) << inp_qubit << ", " << out_qubit;
            ASSERT_EQ(t.xs[inp_qubit].zs[out_qubit], bxz) << inp_qubit << ", " << out_qubit;
            ASSERT_EQ(t.zs[inp_qubit].xs[out_qubit], bzx) << inp_qubit << ", " << out_qubit;
            ASSERT_EQ(t.zs[inp_qubit].zs[out_qubit], bzz) << inp_qubit << ", " << out_qubit;

            {
                TableauTransposedRaii trans(t);
                ASSERT_EQ(t.xs.xt[out_qubit][inp_qubit], bxx);
                ASSERT_EQ(t.xs.zt[out_qubit][inp_qubit], bxz);
                ASSERT_EQ(t.zs.xt[out_qubit][inp_qubit], bzx);
                ASSERT_EQ(t.zs.zt[out_qubit][inp_qubit], bzz);
            }
        }
    }
}

TEST(tableau, inverse) {
    Tableau t1(1);
    ASSERT_EQ(t1, t1.inverse());
    t1.prepend_X(0);
    ASSERT_EQ(t1, GATE_TABLEAUS.at("X"));
    auto t2 = t1.inverse();
    ASSERT_EQ(t1, GATE_TABLEAUS.at("X"));
    ASSERT_EQ(t2, GATE_TABLEAUS.at("X"));

    for (size_t k = 5; k < 20; k += 7) {
        t1 = Tableau::random(k, SHARED_TEST_RNG());
        t2 = t1.inverse();
        ASSERT_TRUE(t2.satisfies_invariants());
        auto p = PauliString::random(k, SHARED_TEST_RNG());
        auto p2 = t1(t2(p));
        auto x1 = p.xs.str();
        auto x2 = p2.xs.str();
        auto z1 = p.zs.str();
        auto z2 = p2.zs.str();
        ASSERT_EQ(p, p2);
        ASSERT_EQ(p, t2(t1(p)));
    }
}

TEST(tableau, prepend_pauli) {
    Tableau t = Tableau::random(6, SHARED_TEST_RNG());
    Tableau ref = t;
    t.prepend(PauliString::from_str("_XYZ__"));
    ref.prepend_X(1);
    ref.prepend_Y(2);
    ref.prepend_Z(3);
    ASSERT_EQ(t, ref);
    t.prepend(PauliString::from_str("Y_ZX__"));
    ref.prepend_X(3);
    ref.prepend_Y(0);
    ref.prepend_Z(2);
    ASSERT_EQ(t, ref);
}