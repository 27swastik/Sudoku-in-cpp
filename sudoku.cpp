#include <array>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <functional>
#include <memory>

using namespace std;

// ---- Utility (portable popcount/ctz) ----
#if defined(_MSC_VER)
  #include <intrin.h>
  #pragma intrinsic(_BitScanForward)
  static inline int bitcount(uint16_t x) { return __popcnt16(x); }
  static inline int ctz(uint16_t x) { unsigned long idx; _BitScanForward(&idx, x); return (int)idx; }
#else
  static inline int bitcount(uint16_t x) { return __builtin_popcount((unsigned)x); }
  static inline int ctz(uint16_t x) { return __builtin_ctz((unsigned)x); }
#endif

struct Metrics { uint64_t nodes=0, backtracks=0; };

struct SolverConfig {
    bool enable_mrv = true;
    bool enable_lcv = true;
    bool enable_forward = true; // includes unit propagation
    bool find_all = false;
};

struct Sudoku {
    using Mask = uint16_t; // low 9 bits used

    array<uint8_t,81> grid{}; // 0 for empty, 1..9 for digits
    array<Mask,9> rowMask{};  // bit set => digit present
    array<Mask,9> colMask{};
    array<Mask,9> boxMask{};

    // Candidate masks (only maintained precisely when forward is enabled)
    array<Mask,81> cand{};

    // Precomputed peers: 20 unique peers per cell
    array<array<uint8_t,20>,81> peers{};

    int unassigned = 81;

    struct Change { uint16_t idx; Mask priorCand; uint8_t priorVal; };
    vector<Change> trail;
    vector<size_t> frames; // indices into trail (frame boundaries)

    Metrics m;
    SolverConfig cfg;

    static constexpr Mask ALL = (Mask)0x1FF; // 9 bits

    static inline int boxOf(int r, int c) { return (r/3)*3 + (c/3); }

    void initPeers() {
        for (int r=0; r<9; ++r) {
            for (int c=0; c<9; ++c) {
                int i = r*9 + c;
                array<uint8_t,20> ps{}; int k=0;
                // row peers
                for (int cc=0; cc<9; ++cc) if (cc!=c) ps[k++] = r*9 + cc;
                // col peers
                for (int rr=0; rr<9; ++rr) if (rr!=r) {
                    int idx = rr*9 + c;
                    bool dup=false; for (int t=0;t<k;++t) if (ps[t]==idx) { dup=true; break; }
                    if (!dup) ps[k++] = idx;
                }
                // box peers
                int br=(r/3)*3, bc=(c/3)*3;
                for (int rr=br; rr<br+3; ++rr) for (int cc=bc; cc<bc+3; ++cc) if (!(rr==r && cc==c)) {
                    int idx = rr*9 + cc;
                    bool dup=false; for (int t=0;t<k;++t) if (ps[t]==idx) { dup=true; break; }
                    if (!dup) ps[k++] = idx;
                }
                // k should be 20
                peers[i] = ps;
            }
        }
    }

    void reset(const SolverConfig& config) {
        cfg = config;
        fill(grid.begin(), grid.end(), 0);
        rowMask.fill(0); colMask.fill(0); boxMask.fill(0);
        cand.fill(ALL);
        unassigned = 81;
        trail.clear(); frames.clear(); m = {};
    }

    bool placeInitial(int idx, int d) {
        int r = idx/9, c = idx%9, b = boxOf(r,c); Mask bit = (Mask)1u << (d-1);
        if ((rowMask[r]&bit) || (colMask[c]&bit) || (boxMask[b]&bit)) return false;
        grid[idx] = (uint8_t)d; --unassigned;
        rowMask[r] |= bit; colMask[c] |= bit; boxMask[b] |= bit;
        return true;
    }

    void computeAllCandidatesFromMasks() {
        for (int i=0;i<81;++i) if (grid[i]==0) {
            int r=i/9, c=i%9, b=boxOf(r,c);
            cand[i] = (Mask)(~(rowMask[r] | colMask[c] | boxMask[b]) & ALL);
        } else {
            cand[i] = (Mask)(1u << (grid[i]-1));
        }
    }

    bool loadFromString(const string& s, string& err) {
        // Accept 81 meaningful chars across the string: digits 1-9, '.' or '0'
        vector<int> vals; vals.reserve(81);
        for (char ch : s) {
            if (ch=='.' || ch=='0') vals.push_back(0);
            else if (ch>='1' && ch<='9') vals.push_back(ch-'0');
            if ((int)vals.size()==81) break;
        }
        if ((int)vals.size()!=81) { err = "expected 81 cells"; return false; }
        // place
        for (int i=0;i<81;++i) if (vals[i]!=0) {
            if (!placeInitial(i, vals[i])) { err = "invalid puzzle: duplicate in a unit"; return false; }
        }
        // initial candidates
        computeAllCandidatesFromMasks();
        return true;
    }

    // ---- Search helpers ----
    void frame_push() { frames.push_back(trail.size()); }

    void frame_pop() {
        size_t tgt = frames.back(); frames.pop_back();
        while (trail.size() > tgt) {
            auto ch = trail.back(); trail.pop_back();
            int i = ch.idx; int r=i/9, c=i%9, b=boxOf(r,c);
            // If grid changed, restore masks
            if (grid[i] != ch.priorVal) {
                if (grid[i] != 0) {
                    Mask bit = (Mask)1u << (grid[i]-1);
                    rowMask[r] &= ~bit; colMask[c] &= ~bit; boxMask[b] &= ~bit;
                    ++unassigned;
                }
                grid[i] = ch.priorVal;
            }
            cand[i] = ch.priorCand;
        }
    }

    bool reduce_peer(int j, Mask bit, vector<int>& singletons) {
        if (grid[j]!=0) return true;
        if (cand[j] & bit) {
            trail.push_back({(uint16_t)j, cand[j], 0});
            cand[j] &= (Mask)~bit;
            int c = bitcount(cand[j]);
            if (c==0) return false; // conflict
            if (cfg.enable_forward && c==1) singletons.push_back(j);
        }
        return true;
    }

    bool assign_with_propagation(int i, int d) {
        int r=i/9, c=i%9, b=boxOf(r,c); Mask bit = (Mask)1u << (d-1);
        // legality check
        if ((rowMask[r]&bit) || (colMask[c]&bit) || (boxMask[b]&bit)) return false;
        // record current state
        trail.push_back({(uint16_t)i, cand[i], grid[i]});
        grid[i]=(uint8_t)d; --unassigned;
        cand[i]=bit; // fixed
        rowMask[r]|=bit; colMask[c]|=bit; boxMask[b]|=bit;

        if (!cfg.enable_forward) return true; // no forward checking / propagation

        // forward check peers
        vector<int> singletons;
        singletons.reserve(10);
        for (int t=0;t<20;++t) {
            int j = peers[i][t];
            if (!reduce_peer(j, bit, singletons)) return false;
        }
        // unit propagation
        while (!singletons.empty()) {
            int j = singletons.back(); singletons.pop_back();
            if (grid[j]!=0) continue; // might have been set already
            Mask cm = cand[j];
            if (bitcount(cm)!=1) continue; // only when singleton
            int d2 = 1 + ctz(cm);
            int r2=j/9, c2=j%9, b2=boxOf(r2,c2); Mask bit2=(Mask)1u<<(d2-1);
            // legality (should hold, but double-check)
            if ((rowMask[r2]&bit2) || (colMask[c2]&bit2) || (boxMask[b2]&bit2)) return false;
            trail.push_back({(uint16_t)j, cand[j], grid[j]});
            grid[j]=(uint8_t)d2; --unassigned; cand[j]=bit2;
            rowMask[r2]|=bit2; colMask[c2]|=bit2; boxMask[b2]|=bit2;
            // propagate to peers of j
            for (int t=0;t<20;++t) {
                int k = peers[j][t];
                if (!reduce_peer(k, bit2, singletons)) return false;
            }
        }
        return true;
    }

    // Select next variable: MRV + degree, else first unassigned
    int select_var() {
        int best=-1, best_cc=10, best_deg=-1;
        for (int i=0;i<81;++i) if (grid[i]==0) {
            int cc;
            if (cfg.enable_mrv) {
                if (cfg.enable_forward) cc = bitcount(cand[i]);
                else {
                    int r=i/9,c=i%9,b=boxOf(r,c);
                    uint16_t cm = (uint16_t)(~(rowMask[r]|colMask[c]|boxMask[b]) & ALL);
                    cc = bitcount(cm);
                }
                if (cc < best_cc) {
                    best=i; best_cc=cc;
                    if (cc==1) { // can't beat
                        int deg=0; for (int t=0;t<20;++t) if (grid[peers[i][t]]==0) ++deg; best_deg=deg;
                        continue;
                    }
                    int deg=0; for (int t=0;t<20;++t) if (grid[peers[i][t]]==0) ++deg; best_deg=deg;
                } else if (cc == best_cc) {
                    int deg=0; for (int t=0;t<20;++t) if (grid[peers[i][t]]==0) ++deg;
                    if (deg > best_deg) { best=i; best_deg=deg; }
                }
            } else {
                // no MRV: first unassigned, row-major
                return i;
            }
        }
        return best;
    }

    // Order values: LCV or ascending digits
    void order_values(int i, vector<int>& out) {
        out.clear();
        uint16_t cm;
        if (cfg.enable_forward) cm = cand[i];
        else {
            int r=i/9,c=i%9,b=boxOf(r,c);
            cm = (uint16_t)(~(rowMask[r]|colMask[c]|boxMask[b]) & ALL);
        }
        for (int d=1; d<=9; ++d) if (cm & (1u<<(d-1))) out.push_back(d);
        if (!cfg.enable_lcv) return; // already ascending
        // compute cost for each d
        vector<pair<int,int>> scored; scored.reserve(out.size());
        for (int d: out) {
            int cost=0; uint16_t bit=(1u<<(d-1));
            for (int t=0;t<20;++t) {
                int j=peers[i][t]; if (grid[j]!=0) continue;
                int r=j/9,c=j%9,b=boxOf(r,c);
                uint16_t cmj;
                if (cfg.enable_forward) cmj = cand[j];
                else cmj = (uint16_t)(~(rowMask[r]|colMask[c]|boxMask[b]) & ALL);
                if (cmj & bit) ++cost;
            }
            scored.emplace_back(cost, d);
        }
        sort(scored.begin(), scored.end(), [](auto& a, auto& b){ if (a.first!=b.first) return a.first<b.first; return a.second<b.second; });
        out.clear(); for (auto& p: scored) out.push_back(p.second);
    }

    bool solveDFS(bool& stop_after_first) {
        if (unassigned==0) return true;
        int i = select_var();
        if (i<0) return false; // should not happen unless contradiction
        vector<int> values; order_values(i, values);
        if (values.empty()) return false;
        for (int d: values) {
            ++m.nodes;
            frame_push();
            if (assign_with_propagation(i,d)) {
                if (solveDFS(stop_after_first)) {
                    if (!cfg.find_all) return true; // keep first; caller will capture
                }
            }
            ++m.backtracks;
            frame_pop();
        }
        return false;
    }

    // Public API
    struct Result { bool valid=true; bool solved=false; int solutions=0; Metrics met; };

    Result solve(const string& puzzle) {
        string err; reset(cfg); initPeers();
        if (!loadFromString(puzzle, err)) return {false,false,0,{}};
        if (cfg.enable_forward) computeAllCandidatesFromMasks();

        Result res; res.valid=true; res.solved=false; res.solutions=0; res.met={};
        vector<array<uint8_t,81>> sols;
        function<bool()> dfs = [&]() -> bool {
            if (unassigned==0) { ++res.solutions; res.solved=true; sols.push_back(grid); return !cfg.find_all; }
            int i = select_var(); if (i<0) return false;
            vector<int> values; order_values(i, values); if (values.empty()) return false;
            for (int d: values) { ++m.nodes; frame_push(); if (assign_with_propagation(i,d)) { if (dfs()) return true; } ++m.backtracks; frame_pop(); }
            return false;
        };
        dfs();
        res.met = m;
        return res;
    }
};

// ------ CLI helpers ------
static string read_one_puzzle_from_stream(istream& in, bool strictLine=false) {
    string acc; acc.reserve(100);
    string line;
    if (strictLine) {
        if (!std::getline(in,line)) return "";
        for (char ch: line) if ((ch>='1'&&ch<='9') || ch=='.' || ch=='0') acc.push_back(ch);
        return acc;
    }
    // flexible: read until we gather 81 valid chars
    while (std::getline(in,line)) {
        for (char ch: line) if ((ch>='1'&&ch<='9') || ch=='.' || ch=='0') acc.push_back(ch);
        if ((int)acc.size()>=81) break;
    }
    return acc;
}

static void print_grid(const array<uint8_t,81>& grid) {
    for (int r=0;r<9;++r) {
        for (int c=0;c<9;++c) cout << int(grid[r*9+c]);
        cout << '\n';
    }
}

static void print_metrics(const Metrics& m, chrono::steady_clock::duration dt, bool csv, const string& label="") {
    using namespace std::chrono;
    double ms = duration<double, std::milli>(dt).count();
    if (csv) {
        if (!label.empty()) cout << label << ',';
        cout << fixed << setprecision(3) << ms << ',' << m.nodes << ',' << m.backtracks << '\n';
    } else {
        if (!label.empty()) cout << label << "\n";
        cout << "time_ms=" << fixed << setprecision(3) << ms
             << " nodes=" << m.nodes
             << " backtracks=" << m.backtracks << "\n";
    }
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc<2) {
        cerr << "Usage: sudoku --solve [FILE|-] [--all] [--no-heuristics] [--no-lcv] [--no-forward] [--csv]\n"
                "       sudoku --bench FILE [--csv]\n";
        return 4;
    }

    bool modeSolve=false, modeBench=false, csv=false;
    string file="-";
    SolverConfig cfg;

    for (int i=1;i<argc;++i) {
        string a=argv[i];
        if (a=="--solve") { modeSolve=true; if (i+1<argc && argv[i+1][0]!='-') { file=argv[++i]; } }
        else if (a=="--bench") { modeBench=true; if (i+1<argc) file=argv[++i]; }
        else if (a=="--all") cfg.find_all=true;
        else if (a=="--no-heuristics") { cfg.enable_mrv=false; cfg.enable_lcv=false; cfg.enable_forward=false; }
        else if (a=="--no-lcv") cfg.enable_lcv=false;
        else if (a=="--no-forward") cfg.enable_forward=false;
        else if (a=="--csv") csv=true;
        else { cerr << "Unknown arg: "<<a<<"\n"; return 4; }
    }

    if (modeSolve == modeBench) { cerr << "Choose exactly one of --solve or --bench\n"; return 4; }

    if (modeSolve) {
        unique_ptr<ifstream> fsp; // keep ownership if reading from file
        istream* inptr = nullptr;
        if (file=="-" || file.empty()) inptr = &cin;
        else {
            fsp = make_unique<ifstream>(file);
            if (!*fsp) { cerr << "IO error opening file\n"; return 4; }
            inptr = fsp.get();
        }
        string src = read_one_puzzle_from_stream(*inptr, false);
        Sudoku s; s.initPeers(); s.reset(cfg);
        string err;
        if (!s.loadFromString(src, err)) { cerr << err << "\n"; return 3; }
        if (cfg.enable_forward) s.computeAllCandidatesFromMasks();
        auto t0 = chrono::steady_clock::now();
        // inline DFS that captures solutions
        vector<array<uint8_t,81>> sols;
        function<bool(Sudoku&)> dfs = [&](Sudoku& S)->bool{
            if (S.unassigned==0) { sols.push_back(S.grid); return !S.cfg.find_all; }
            int i = S.select_var(); if (i<0) return false;
            vector<int> vals; S.order_values(i, vals); if (vals.empty()) return false;
            for (int d: vals) { ++S.m.nodes; S.frame_push(); if (S.assign_with_propagation(i,d)) { if (dfs(S)) return true; } ++S.m.backtracks; S.frame_pop(); }
            return false;
        };
        dfs(s);
        auto t1 = chrono::steady_clock::now();
        if (sols.empty()) { cerr << "unsolvable\n"; print_metrics(s.m, t1-t0, csv); return 2; }
        for (size_t k=0;k<sols.size();++k) {
            if (s.cfg.find_all && !csv) cout << "Solution "<< (k+1) << ":\n";
            print_grid(sols[k]);
            if (!csv) cout << '\n';
        }
        print_metrics(s.m, t1-t0, csv);
        return 0;
    }

    if (modeBench) {
        ifstream fs(file);
        if (!fs) { cerr << "IO error opening bench file\n"; return 4; }
        vector<string> lines; string line;
        while (getline(fs,line)) {
            // Extract up to 81 allowed chars per line
            string acc; for (char ch: line) if ((ch>='1'&&ch<='9')||ch=='.'||ch=='0') acc.push_back(ch);
            if ((int)acc.size()>=81) { acc=acc.substr(0,81); lines.push_back(acc); }
        }
        if (lines.empty()) { cerr << "No valid puzzles in bench file\n"; return 4; }
        if (csv) cout << "label,ms,nodes,backtracks\n";

        auto runVariant = [&](const string& label, const SolverConfig& cfgV){
            auto t0 = chrono::steady_clock::now();
            Metrics total{};
            for (size_t i=0;i<lines.size();++i) {
                Sudoku s; s.initPeers(); s.reset(cfgV);
                string err; if (!s.loadFromString(lines[i], err)) continue; if (cfgV.enable_forward) s.computeAllCandidatesFromMasks();
                // Solve one; stop at first solution
                function<bool(Sudoku&)> dfs = [&](Sudoku& S)->bool{
                    if (S.unassigned==0) return true;
                    int idx = S.select_var(); if (idx<0) return false;
                    vector<int> vals; S.order_values(idx, vals); if (vals.empty()) return false;
                    for (int d: vals) { ++S.m.nodes; S.frame_push(); if (S.assign_with_propagation(idx,d)) { if (dfs(S)) return true; } ++S.m.backtracks; S.frame_pop(); }
                    return false;
                };
                dfs(s);
                total.nodes += s.m.nodes; total.backtracks += s.m.backtracks;
            }
            auto t1 = chrono::steady_clock::now();
            print_metrics(total, t1-t0, csv, label);
        };

        // Heuristic run
        runVariant("heuristics", cfg);
        // Baseline run (plain backtracking)
        SolverConfig base = cfg; base.enable_mrv=false; base.enable_lcv=false; base.enable_forward=false; base.find_all=false;
        runVariant("plain", base);
        return 0;
    }

    return 0;
}
