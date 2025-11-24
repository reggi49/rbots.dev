/**
 * @format
 */

import { AppRegistry } from 'react-native';
// Use the App located in `./src/App.tsx` (our Otak Robot Belajar entry)
import App from './src/App';
import { name as appName } from './app.json';

AppRegistry.registerComponent(appName, () => App);
