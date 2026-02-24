import React from 'react';
import ReactDOM from 'react-dom/client';
import { FluentProvider } from '@fluentui/react-components';
import { darkTheme } from './theme';
import { App } from './App';
import './index.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <FluentProvider theme={darkTheme} style={{ height: '100%' }}>
      <App />
    </FluentProvider>
  </React.StrictMode>
);
