/**
 * Script to add dispose() calls to all DspProcessor/Pipeline instances in test files
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const testsDir = path.join(__dirname, "../src/ts/__tests__");

// Files that already have dispose() implemented
const filesWithDispose = [
  "MovingAverage.test.ts",
  "Chaining.test.ts",
  "ChannelMerge.test.ts",
  "WillisonAmplitude.test.ts",
  "WaveformLength.test.ts",
  "WaveletHilbert.test.ts",
];

function addDisposeToFile(filePath) {
  const filename = path.basename(filePath);

  // Skip files that already have dispose
  if (filesWithDispose.includes(filename)) {
    console.log(`✓ Skipping ${filename} (already has dispose)`);
    return;
  }

  let content = fs.readFileSync(filePath, "utf-8");
  let modified = false;

  // Pattern 1: Files with beforeEach creating processor variable
  // Look for: let processor: DspProcessor; with beforeEach
  if (
    content.includes("let processor: DspProcessor") ||
    content.includes("let pipeline: DspProcessor")
  ) {
    const processorName = content.includes("let processor:")
      ? "processor"
      : "pipeline";

    // Check if afterEach already exists
    if (!content.includes("afterEach(")) {
      // Find the beforeEach block
      const beforeEachMatch = content.match(
        /beforeEach\(\(\) => \{[\s\S]*?\}\);/
      );
      if (beforeEachMatch) {
        const insertPos = beforeEachMatch.index + beforeEachMatch[0].length;
        const afterEachBlock = `\n\n  afterEach(() => {\n    ${processorName}.dispose();\n  });`;
        content =
          content.slice(0, insertPos) +
          afterEachBlock +
          content.slice(insertPos);
        modified = true;
        console.log(`✓ Added afterEach dispose() to ${filename}`);
      }
    }
  }

  // Pattern 2: Individual test cases creating pipeline inline
  // Look for: const pipeline = createDspPipeline()
  if (
    content.includes("const pipeline = createDspPipeline()") ||
    content.includes("const processor = createDspPipeline()")
  ) {
    // Find all test cases
    const testRegex = /(test|it)\([^{]+\{[\s\S]*?\n  \}\);/g;
    let match;
    const replacements = [];

    while ((match = testRegex.exec(content)) !== null) {
      const testBody = match[0];

      // Check if this test creates a pipeline
      const pipelineMatches = testBody.match(
        /const (pipeline|processor\d*) = createDspPipeline\(\)/g
      );

      if (pipelineMatches && !testBody.includes(".dispose()")) {
        // Extract pipeline variable names
        const varNames = pipelineMatches
          .map((m) => {
            const nameMatch = m.match(/const (\w+) =/);
            return nameMatch ? nameMatch[1] : null;
          })
          .filter(Boolean);

        // Find the closing brace of the test
        const closingBraceMatch = testBody.match(/\n  \}\);$/);
        if (closingBraceMatch) {
          const disposeStatements = varNames
            .map((name) => `      ${name}.dispose();`)
            .join("\n");
          const newTestBody = testBody.replace(
            /\n  \}\);$/,
            `\n\n${disposeStatements}\n  });`
          );
          replacements.push({ old: testBody, new: newTestBody });
        }
      }
    }

    // Apply replacements
    for (const { old, new: newText } of replacements) {
      content = content.replace(old, newText);
      modified = true;
    }

    if (replacements.length > 0) {
      console.log(
        `✓ Added inline dispose() calls to ${replacements.length} tests in ${filename}`
      );
    }
  }

  if (modified) {
    fs.writeFileSync(filePath, content);
    console.log(`✓ Modified ${filename}`);
  } else {
    console.log(`  No changes needed for ${filename}`);
  }
}

// Process all test files
const testFiles = fs
  .readdirSync(testsDir)
  .filter((f) => f.endsWith(".test.ts"))
  .map((f) => path.join(testsDir, f));

console.log(`Processing ${testFiles.length} test files...\n`);

for (const file of testFiles) {
  try {
    addDisposeToFile(file);
  } catch (error) {
    console.error(`✗ Error processing ${path.basename(file)}:`, error.message);
  }
}

console.log("\n✅ Done!");
