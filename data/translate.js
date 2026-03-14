// The locale our app first shows
const defaultLocale = "en";
const supportedLocales = ["en", "fr"];
let locale;
// Gets filled with active locale translations
let translations = {};
// When the page content is ready...
document.addEventListener("DOMContentLoaded", () => {
    // Get user defined locales
    const initialLocale = supportedOrDefault(browserLocales(true));
    // Translate the page to required (or default) locale
    setLocale(initialLocale);
    bindLocaleSwitcher(initialLocale);
});

// Load translations for the given locale and translate the page to this locale
async function setLocale(newLocale) {
    if (newLocale === locale) return;
    const newTranslations = await fetchTranslationsFor(newLocale);
    locale = newLocale;
    translations = newTranslations;
    translatePage();
}

// Retrieve translations JSON object for the given locale over the network
async function fetchTranslationsFor(newLocale) {
    const response = await fetch(`/lang_${newLocale}.json`);
    return await response.json();
}

// Replace the inner text of each element that has a data-i18n-key attribute
function translatePage() {
    document.querySelectorAll("[data-i18n-key]").forEach(translateElement);
    let pageName = location.pathname.substr(1).replace("/","_");
    const dotPosition = pageName.lastIndexOf(".");
    if (dotPosition > 0) {
        pageName = pageName.substring(0,dotPosition);
    }
    document.title = translations[pageName+"Title"];
}

// Replace the inner text of the given HTML element with the translation in the active locale
function translateElement(element) {
    const key = element.getAttribute("data-i18n-key");
    const translation = translations[key];
    if (element.nodeName == "INPUT" || element.nodeName == "BUTTON") {
        element.value = translation;
    } else {
        element.innerHTML = translation;
    }
}

// Whenever the user selects a new locale, we load the locale's translations and update the page
function bindLocaleSwitcher(initialValue) {
    const switcher = document.querySelector("[data-i18n-switcher]");
    if (switcher) {
        switcher.value = initialValue;
        switcher.onchange = (e) => {
            // Set the locale to the selected option[value]
            setLocale(e.target.value);
        };
    }
}

// Returns user-preferred locales array (only in 2 letters code if languageCodeOnly is true)
function browserLocales(languageCodeOnly = false) {
    return navigator.languages.map((locale) => languageCodeOnly ? locale.split("-")[0] : locale, );
}

// Check a local against list of supported ones
function isSupported(locale) {
    return supportedLocales.indexOf(locale) > -1;
}

// Retrieve the first locale we support from the given array, or default locale
function supportedOrDefault(locales) {
    return locales.find(isSupported) || defaultLocale;
}