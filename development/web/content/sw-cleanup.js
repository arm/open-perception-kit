if ("serviceWorker" in navigator) {
    navigator.serviceWorker.getRegistrations()
        .then((registrations) => Promise.all(registrations.map((registration) => registration.unregister())))
        .catch((error) => console.warn("Service worker cleanup failed", error));
}

if ("caches" in window) {
    caches.keys()
        .then((keys) => Promise.all(keys.map((key) => caches.delete(key))))
        .catch((error) => console.warn("Cache cleanup failed", error));
}
