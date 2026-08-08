import type { NextConfig } from "next";

// Static export: the site is served as plain HTML/CSS/JS by Caddy on the Oracle
// box (no Vercel, no Node runtime). All dynamic behaviour lives in the Python
// backend behind /api/* — redirects() would need a server, so /login -> / is
// handled in the Caddyfile instead.
const nextConfig: NextConfig = {
  output: "export",
};

export default nextConfig;
