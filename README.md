# 3D Fractal Explorer

This project is a web-based **3D Fractal Explorer** built with **Three.js** and **WebGL**. It uses ray-marching to render complex 3D fractals in real-time.

## How to Run

This is a static web application. To run it locally, you need to serve the files using a local web server. Opening `index.html` directly in the browser via `file://` may cause issues with loading local resources due to CORS restrictions.

You can use any local development server. Here are a few common options:

### Recommended: Using the custom Dev Server (Fixes Windows MIME issues)
If you are on Windows and encounter a "MIME type" error, use the provided `dev-server.py` script:

```bash
python dev-server.py
```

Then open [http://localhost:8000](http://localhost:8000) in your web browser.

### Alternative: Using Python
If you don't want to use the script, you can try the standard module:

```bash
# Python 3
python -m http.server 8000
```

Then open [http://localhost:8000](http://localhost:8000) in your web browser.

### Using Node.js
If you have Node.js installed, you can use `serve`:

```bash
npx serve
```

Then open the provided local address (usually [http://localhost:3000](http://localhost:3000)) in your web browser.

### Using VS Code Live Server
If you use Visual Studio Code, you can install the **Live Server** extension:
1. Open the project in VS Code.
2. Right-click on `index.html`.
3. Select **Open with Live Server**.
