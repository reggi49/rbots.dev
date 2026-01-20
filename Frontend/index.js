/**
 * @format
 */

// Polyfills for base64 and Buffer used by AudioBuffer utility.
// These are safe no-ops if packages are not installed yet; after running
// `npm install base-64 buffer` these will ensure runtime decoding works.
if (typeof globalThis.atob === 'undefined') {
	try {
		// lazy-require so Metro doesn't error during static analysis
		// (the package will be available after npm install)
		globalThis.atob = (s) => require('base-64').decode(s);
	} catch (e) {
		// leave undefined; AudioBuffer has fallbacks for missing atob
	}
}
if (typeof globalThis.btoa === 'undefined') {
	try {
		globalThis.btoa = (s) => require('base-64').encode(s);
	} catch (e) {}
}
if (typeof globalThis.Buffer === 'undefined') {
	try {
		globalThis.Buffer = require('buffer').Buffer;
	} catch (e) {}
}

import { AppRegistry } from 'react-native';
// Use the App located in `./src/App.tsx` (our Otak Robot Belajar entry)
import App from './src/App';
import { name as appName } from './app.json';

AppRegistry.registerComponent(appName, () => App);
