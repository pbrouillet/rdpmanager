import {
  webDarkTheme,
  type Theme,
  type BrandVariants,
} from '@fluentui/react-components';

const rdpBrand: BrandVariants = {
  10: '#020305',
  20: '#101820',
  30: '#15243A',
  40: '#1A2E4E',
  50: '#1F3963',
  60: '#244479',
  70: '#2A508F',
  80: '#0091FF',
  90: '#4BA3FF',
  100: '#6DB3FF',
  110: '#8BC2FF',
  120: '#A6D1FF',
  130: '#BFDFFF',
  140: '#D6EBFF',
  150: '#ECF5FF',
  160: '#F8FBFF',
};

export const darkTheme: Theme = {
  ...webDarkTheme,
  colorBrandForeground1: rdpBrand[80],
  colorBrandForeground2: rdpBrand[90],
  colorBrandBackground: rdpBrand[80],
  colorBrandBackground2: rdpBrand[30],
  colorBrandStroke1: rdpBrand[80],
  colorBrandStroke2: rdpBrand[60],
  colorCompoundBrandForeground1: rdpBrand[80],
  colorCompoundBrandBackground: rdpBrand[80],
  colorCompoundBrandStroke: rdpBrand[80],
  colorCompoundBrandForeground1Hover: rdpBrand[90],
  colorCompoundBrandBackgroundHover: rdpBrand[90],
  colorCompoundBrandStrokeHover: rdpBrand[90],
  colorCompoundBrandForeground1Pressed: rdpBrand[70],
  colorCompoundBrandBackgroundPressed: rdpBrand[70],
  colorCompoundBrandStrokePressed: rdpBrand[70],
};
