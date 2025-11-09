#!/usr/bin/env node

/**
 * Smart installation script for dspx native addon
 *
 * Strategy:
 * - ARM architectures: Always compile locally (better performance tuning)
 * - x64/ia32: Use prebuilt binaries from node-gyp-build (avoids C++ toolchain requirement)
 *
 * This ensures:
 * 1. Most users (x64) get fast installs without needing build tools
 * 2. ARM users get optimized builds for their specific hardware
 */

import { arch, platform } from "os";
import { execSync } from "child_process";
import { existsSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, "..");

// Detect architecture
const architecture = arch();
const isArm = architecture.includes("arm") || architecture.includes("aarch");

console.log(
  `\nDetected platform: ${platform()}, architecture: ${architecture}`
);

if (isArm) {
  console.log(
    "ARM architecture detected - compiling native addon locally for optimal performance..."
  );

  try {
    // Check if node-gyp is available
    try {
      execSync("node-gyp --version", { stdio: "ignore" });
    } catch (e) {
      console.log("Installing node-gyp...");
      execSync("npm install -g node-gyp", { stdio: "inherit" });
    }

    // Compile the native addon
    console.log("Building native addon...");
    execSync("node-gyp rebuild", {
      cwd: projectRoot,
      stdio: "inherit",
      env: { ...process.env },
    });

    console.log("Native addon compiled successfully!");
  } catch (error) {
    console.error("Failed to compile native addon:");
    console.error(error.message);
    console.error("\nMake sure you have C++ build tools installed:");
    console.error("   - macOS: xcode-select --install");
    console.error("   - Linux: sudo apt-get install build-essential");
    console.error("   - Windows: npm install --global windows-build-tools");
    process.exit(1);
  }
} else {
  console.log("x64 architecture detected - using prebuilt binary...");

  // Check if prebuild exists
  const prebuildsDir = join(projectRoot, "prebuilds");

  if (!existsSync(prebuildsDir)) {
    console.warn(
      "No prebuilds directory found. Falling back to local compilation..."
    );

    try {
      execSync("node-gyp rebuild", {
        cwd: projectRoot,
        stdio: "inherit",
      });
      console.log("Native addon compiled successfully!");
    } catch (error) {
      console.error("Failed to compile native addon:");
      console.error(error.message);
      console.error(
        "\nThis package requires prebuilt binaries or a C++ toolchain."
      );
      console.error(
        "   Please report this issue at: https://github.com/A-KGeorge/dspx/issues"
      );
      process.exit(1);
    }
  } else {
    console.log("Using prebuilt binary - no compilation needed!");
  }
}

console.log("Installation complete!\n");
