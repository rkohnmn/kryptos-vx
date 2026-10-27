#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — Embedded Vector SVG Logo & In-App ANSI Emblem
// Zero-dependency embedded branding for CLI, TUI, MCP Server, and Desktop Icon
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/simd_scanner.hpp"
#include <iostream>
#include <string_view>

namespace kryptos::ui {

class BrandLogo {
public:
    static constexpr std::string_view EMBEDDED_SVG = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256" fill="none">
  <defs>
    <linearGradient id="bg_grad" x1="0" y1="0" x2="256" y2="256" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#0B0F17"/>
      <stop offset="100%" stop-color="#05070B"/>
    </linearGradient>
    <linearGradient id="cyan_grad" x1="48" y1="44" x2="156" y2="212" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#00F5D4"/>
      <stop offset="100%" stop-color="#00A8E8"/>
    </linearGradient>
    <linearGradient id="emerald_grad" x1="116" y1="44" x2="212" y2="212" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#38B000"/>
      <stop offset="100%" stop-color="#00F5D4"/>
    </linearGradient>
    <linearGradient id="border_grad" x1="0" y1="0" x2="256" y2="256" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#1E293B"/>
      <stop offset="50%" stop-color="#00F5D4" stop-opacity="0.45"/>
      <stop offset="100%" stop-color="#0F172A"/>
    </linearGradient>
  </defs>

  <!-- Precision Rounded Squircle Base -->
  <rect x="8" y="8" width="240" height="240" rx="44" fill="url(#bg_grad)" stroke="url(#border_grad)" stroke-width="4"/>

  <!-- Subtle Technical Coordinate Grid -->
  <g stroke="#1E293B" stroke-width="1" opacity="0.55">
    <line x1="64" y1="10" x2="64" y2="246"/>
    <line x1="128" y1="10" x2="128" y2="246"/>
    <line x1="192" y1="10" x2="192" y2="246"/>
    <line x1="10" y1="64" x2="246" y2="64"/>
    <line x1="10" y1="128" x2="246" y2="128"/>
    <line x1="10" y1="192" x2="246" y2="192"/>
  </g>

  <!-- Outer Geometric Octagon Frame -->
  <polygon points="88,28 168,28 228,88 228,168 168,228 88,228 28,168 28,88"
           fill="none" stroke="#1E293B" stroke-width="2" stroke-dasharray="6 4"/>

  <!-- Left Vertical Stem of 'K' -->
  <path d="M56 56 H84 V200 H56 Z" fill="url(#cyan_grad)"/>

  <!-- Upper Diagonal Arm of 'K' -->
  <polygon points="88,124 140,56 172,56 114,132" fill="url(#cyan_grad)"/>

  <!-- Interlocking 'K' Lower Leg + 'V' / 'X' Vector Blade -->
  <polygon points="96,120 152,200 184,200 124,114" fill="url(#emerald_grad)"/>

  <!-- Right High-Velocity Vector Wing ('V' Ascender) -->
  <polygon points="152,200 184,200 212,56 182,56" fill="url(#cyan_grad)" opacity="0.92"/>

  <!-- Precision SIMD Register Corner Nodes -->
  <circle cx="56" cy="56" r="3.5" fill="#00F5D4"/>
  <circle cx="200" cy="56" r="3.5" fill="#00F5D4"/>
  <circle cx="56" cy="200" r="3.5" fill="#00A8E8"/>
  <circle cx="168" cy="200" r="3.5" fill="#38B000"/>
</svg>
)SVG";

    static void render_in_app_banner(std::ostream& os, bool /*has_avx2*/, bool use_color = true) {
        const char* c_cy  = use_color ? "\033[38;5;51m"  : "";
        const char* c_em  = use_color ? "\033[38;5;48m"  : "";
        const char* c_bl  = use_color ? "\033[38;5;39m"  : "";
        const char* c_dim = use_color ? "\033[2m"        : "";
        const char* c_bld = use_color ? "\033[1m"        : "";
        const char* c_rst = use_color ? "\033[0m"        : "";

        os << "\n"
           << c_cy  << "  ╭──────────────╮  " << c_bld << "╔════════════════════════════════════════════════════════════╗\n" << c_rst
           << c_cy  << "  │ " << c_bld << "██╗  ██╗" << c_em << "██╗  ██╗" << c_cy << " │  " << c_bld << "║  ⚡ KRYPTOS-VX v3.0.0 \"Apex\" — C++20 Full-Stack Systems Engine║\n" << c_rst
           << c_cy  << "  │ " << c_bld << "██║ ██╔╝" << c_em << "██║  ██║" << c_cy << " │  " << c_bld << "║  Engines     : Kernel I/O • Trigram • MCP • ADB • FastCDC  ║\n" << c_rst
           << c_bl  << "  │ " << c_bld << "█████╔╝ " << c_em << "╚██╗██╔╝" << c_bl << " │  " << c_bld << "║  SIMD Vector : " << core::SimdScanner::active_isa_name() << " ║\n" << c_rst
           << c_bl  << "  │ " << c_bld << "██╔═██╗ " << c_em << " ╚███╔╝ " << c_bl << " │  " << c_bld << "║  Binary Prof : ELF64 / Win32 PE / Mach-O64 (`binprof`)     ║\n" << c_rst
           << c_em  << "  │ " << c_bld << "██║  ██╗" << c_cy << "  ███║  " << c_em << " │  " << c_bld << "╚════════════════════════════════════════════════════════════╝\n" << c_rst
           << c_em  << "  ╰──────────────╯  " << c_dim << "   [KRYPTOS-VX v3.0 Apex • All 4 Roadmap Phases Complete]" << c_rst << "\n\n";
    }
};

} // namespace kryptos::ui
