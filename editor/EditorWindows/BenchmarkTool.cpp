#include "BenchmarkTool.h"
#include <imgui.h>
#include <iostream>
#include <iomanip>

namespace editor::editorWindows {

    // =========================================================================
    // Static member definitions
    // =========================================================================
    int         BenchmarkTool::s_iterationsPerRun = 100;
    bool        BenchmarkTool::s_autoRecompute    = true;
    std::string BenchmarkTool::s_csvOutputPath    = "benchmark_results.csv";
    std::string BenchmarkTool::s_baseOutputPath   = "Assets/Data/";

    std::vector<BenchmarkTool::RegisteredBenchmark> BenchmarkTool::s_registered;
    std::vector<BenchmarkEntry>                     BenchmarkTool::s_entries;

    int  BenchmarkTool::s_selectedEntry = -1;
    char BenchmarkTool::s_pathBuf[512]  = {};

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void BenchmarkTool::OnEnable() {
        // Nothing to do — static data persists across enable/disable
    }

    void BenchmarkTool::OnGUI() {
        // ── Top bar ───────────────────────────────────────────────────────────
        ImGui::Text("Iterations per run:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::InputInt("##iters", &s_iterationsPerRun);
        if (s_iterationsPerRun < 1) s_iterationsPerRun = 1;

        ImGui::SameLine(0, 20.f);
        ImGui::SetNextItemWidth(240.f);
        if (s_pathBuf[0] == '\0')
            std::snprintf(s_pathBuf, sizeof(s_pathBuf), "%s", s_csvOutputPath.c_str());
        if (ImGui::InputText("Output CSV", s_pathBuf, sizeof(s_pathBuf)))
            s_csvOutputPath = s_pathBuf;

        ImGui::Separator();

        // ── Action buttons ────────────────────────────────────────────────────
        if (ImGui::Button("Run All"))     RunAll();
        ImGui::SameLine();
        if (ImGui::Button("Export CSV")) {
            if (ExportCSV()) ImGui::OpenPopup("export_ok");
            else             ImGui::OpenPopup("export_fail");
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Results")) {
            for (auto& e : s_entries) e.samples.clear();
        }

        if (ImGui::BeginPopup("export_ok")) {
            ImGui::Text("Saved to: %s", s_csvOutputPath.c_str());
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("export_fail")) {
            ImGui::TextColored({1, 0, 0, 1}, "Failed to write CSV!");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // ── Results table ─────────────────────────────────────────────────────
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders        |
            ImGuiTableFlags_RowBg          |
            ImGuiTableFlags_ScrollY        |
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_Resizable;

        const float tableHeight = s_selectedEntry >= 0 ? 220.f : 0.f;
        if (ImGui::BeginTable("##results", 7, tableFlags, ImVec2(0, tableHeight))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 60.f);
            ImGui::TableSetupColumn("Min ms",  ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("Max ms",  ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("Mean ms", ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("Median",  ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("StdDev",  ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(s_entries.size()); ++i) {
                const auto& e = s_entries[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool selected = (s_selectedEntry == i);
                if (ImGui::Selectable(e.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    s_selectedEntry = selected ? -1 : i;

                ImGui::TableSetColumnIndex(1); ImGui::Text("%zu",  e.samples.size());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f", e.minVal);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", e.maxVal);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.4f", e.meanVal);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%.4f", e.medianVal);
                ImGui::TableSetColumnIndex(6); ImGui::Text("%.4f", e.stdDev);
            }
            ImGui::EndTable();
        }

        // ── Sparkline ─────────────────────────────────────────────────────────
        if (s_selectedEntry >= 0 &&
            s_selectedEntry < static_cast<int>(s_entries.size()))
        {
            const auto& e = s_entries[s_selectedEntry];
            if (!e.samples.empty()) {
                std::vector<float> fsamples;
                fsamples.reserve(e.samples.size());
                std::transform(e.samples.begin(), e.samples.end(), std::back_inserter(fsamples),
                    [](double d) { return static_cast<float>(d); });
                ImGui::Text("Sample history: %s", e.name.c_str());
                ImGui::PlotLines("##sparkline",
                                 fsamples.data(),
                                 static_cast<int>(fsamples.size()),
                                 0, nullptr,
                                 static_cast<float>(e.minVal),
                                 static_cast<float>(e.maxVal),
                                 ImVec2(-1, 80));
            }
        }
    }

    // =========================================================================
    // Registration
    // =========================================================================

    void BenchmarkTool::RegisterBenchmark(const std::string& name,
                                          std::function<void(int)> fn) {
        for (auto& rb : s_registered) {
            if (rb.name == name) { rb.fn = std::move(fn); return; }
        }
        s_registered.push_back({name, std::move(fn)});
        GetOrCreateEntry(name);
    }

    void BenchmarkTool::RemoveBenchmark(const std::string& name) {
        s_registered.erase(
            std::remove_if(s_registered.begin(), s_registered.end(),
                [&](const RegisteredBenchmark& rb){ return rb.name == name; }),
            s_registered.end());
    }

    void BenchmarkTool::ClearBenchmarks() {
        s_registered.clear();
        s_entries.clear();
        s_selectedEntry = -1;
    }

    // =========================================================================
    // Sample injection
    // =========================================================================

    void BenchmarkTool::PushSample(const std::string& name, double valueMs) {
        auto& entry = GetOrCreateEntry(name);
        entry.samples.push_back(valueMs);
        if (s_autoRecompute) entry.Recompute();
    }

    void BenchmarkTool::PushTaggedSample(const std::string& paramName,
                                         double paramValue, double valueMs) {
        std::ostringstream tag;
        tag << paramName << "=" << paramValue;
        PushSample(tag.str(), valueMs);
    }

    // =========================================================================
    // Running
    // =========================================================================

    void BenchmarkTool::RunBenchmark(const std::string& name) {
        auto it = std::find_if(s_registered.begin(), s_registered.end(),
            [&](const RegisteredBenchmark& rb){ return rb.name == name; });

        if (it == s_registered.end()) {
            std::cerr << "[BenchmarkTool] Unknown benchmark: " << name << "\n";
            return;
        }

        auto& entry = GetOrCreateEntry(name);
        entry.samples.reserve(entry.samples.size() + s_iterationsPerRun);

        for (int i = 0; i < s_iterationsPerRun; ++i) {
            using Clock = std::chrono::high_resolution_clock;
            auto t0 = Clock::now();
            it->fn(i);
            auto t1 = Clock::now();
            entry.samples.push_back(
                std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        entry.Recompute();
    }

    void BenchmarkTool::RunAll() {
        for (const auto& rb : s_registered)
            RunBenchmark(rb.name);
    }

    // =========================================================================
    // CSV export
    // =========================================================================

    bool BenchmarkTool::ExportCSV(const std::string& path) {
        const std::string& outPath = s_baseOutputPath + (path.empty() ? s_csvOutputPath : path);
        return WriteCSV(outPath, s_entries);
    }

    bool BenchmarkTool::WriteCSV(const std::string& outPath, const std::vector<BenchmarkEntry>& entries) {
        std::ofstream ofs(outPath);
        if (!ofs.is_open()) {
            std::cerr << "[BenchmarkTool] Cannot open: " << outPath << "\n";
            return false;
        }

        size_t maxSamples = 0;
        for (const auto& e : entries)
            maxSamples = std::max(maxSamples, e.samples.size());

        ofs << "Name,Samples,Min_ms,Max_ms,Mean_ms,Median_ms,StdDev_ms";
        for (size_t i = 0; i < maxSamples; ++i) ofs << ",Sample_" << i;
        ofs << "\n";

        ofs << std::fixed << std::setprecision(6);
        for (const auto& e : entries) {
            // Quote names containing commas
            std::string safeName = e.name;
            if (safeName.find(',') != std::string::npos) {
                std::string escaped;
                for (char c : safeName) { escaped += c; if (c == '"') escaped += '"'; }
                safeName = '"' + escaped + '"';
            }

            ofs << safeName         << ','
                << e.samples.size() << ','
                << e.minVal         << ','
                << e.maxVal         << ','
                << e.meanVal        << ','
                << e.medianVal      << ','
                << e.stdDev;

            for (double s : e.samples) ofs << ',' << s;
            for (size_t i = e.samples.size(); i < maxSamples; ++i) ofs << ',';
            ofs << '\n';
        }
        return true;
    }

    // =========================================================================
    // Private helpers
    // =========================================================================

    BenchmarkEntry& BenchmarkTool::GetOrCreateEntry(const std::string& name) {
        for (auto& e : s_entries)
            if (e.name == name) return e;
        s_entries.push_back({name});
        return s_entries.back();
    }

} // namespace editor::editorWindows