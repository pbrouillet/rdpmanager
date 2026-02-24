import { webDarkTheme, webLightTheme, type Theme } from '@fluentui/react-components';

export type ThemeMode = 'light' | 'dark';

export function getFluentTheme(mode: ThemeMode): Theme {
  return mode === 'dark' ? webDarkTheme : webLightTheme;
}
