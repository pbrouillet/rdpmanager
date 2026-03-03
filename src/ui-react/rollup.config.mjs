import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import typescript from '@rollup/plugin-typescript';
import replace from '@rollup/plugin-replace';
import terser from '@rollup/plugin-terser';
import css from 'rollup-plugin-css-only';
import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { join } from 'path';

const production = !process.env.ROLLUP_WATCH;

// Output directory (overridable via environment variable for meson integration)
const outDir = process.env.ROLLUP_OUT_DIR || 'dist';

/**
 * Custom plugin to generate index.html with hashed asset references.
 * Reads the template index.html at build end and injects the generated
 * JS bundle and CSS file.
 */
function htmlPlugin() {
  return {
    name: 'generate-html',
    writeBundle(options, bundle) {
      const jsFile = Object.keys(bundle).find((f) => f.endsWith('.js'));
      const cssExists = Object.keys(bundle).some((f) => f.endsWith('.css'));

      const template = readFileSync('index.html', 'utf-8');

      let html = template;

      // Inject CSS link before </head>
      if (cssExists) {
        html = html.replace(
          '</head>',
          `  <link rel="stylesheet" href="./assets/index.css">\n</head>`
        );
      }

      // Replace the dev <script type="module" src="/src/main.tsx"> with the built bundle
      html = html.replace(
        /<script type="module" src="\/src\/main\.tsx"><\/script>/,
        jsFile ? `<script defer src="./${jsFile}"></script>` : ''
      );

      const dir = options.dir || 'dist';
      mkdirSync(dir, { recursive: true });
      writeFileSync(join(dir, 'index.html'), html);
    },
  };
}

export default {
  input: 'src/main.tsx',
  output: {
    dir: outDir,
    entryFileNames: 'assets/[name]-[hash].js',
    chunkFileNames: 'assets/[name]-[hash].js',
    format: 'iife',
    sourcemap: !production,
    // Inline dynamic imports since IIFE doesn't support code splitting
    inlineDynamicImports: true,
  },
  plugins: [
    replace({
      preventAssignment: true,
      values: {
        'process.env.NODE_ENV': JSON.stringify(production ? 'production' : 'development'),
      },
    }),
    resolve({
      browser: true,
      extensions: ['.ts', '.tsx', '.js', '.jsx', '.json'],
      dedupe: ['react', 'react-dom'],
    }),
    commonjs(),
    typescript({
      tsconfig: './tsconfig.json',
      sourceMap: !production,
      inlineSources: !production,
    }),
    css({ output: 'assets/index.css' }),
    production && terser({
      compress: {
        drop_console: false,
        passes: 2,
      },
    }),
    htmlPlugin(),
  ],
  onwarn(warning, warn) {
    // Suppress circular dependency warnings from Fluent UI internals
    if (warning.code === 'CIRCULAR_DEPENDENCY') return;
    // Suppress "use client" directive warnings from React
    if (warning.code === 'MODULE_LEVEL_DIRECTIVE' && warning.message.includes('"use client"')) return;
    warn(warning);
  },
};
