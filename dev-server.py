import http.server
import socketserver
import sys

PORT = 8000

class FractalHandler(http.server.SimpleHTTPRequestHandler):
    # Override guess_type to completely bypass Windows registry for these extensions
    def guess_type(self, path):
        if path.endswith('.js') or path.endswith('.mjs'):
            return 'application/javascript'
        if path.endswith('.css'):
            return 'text/css'
        if path.endswith('.html'):
            return 'text/html'
        return super().guess_type(path)

    def end_headers(self):
        # Add CORS headers just in case
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

if __name__ == "__main__":
    try:
        # allow_reuse_address prevents "Address already in use" errors if restarted quickly
        socketserver.TCPServer.allow_reuse_address = True
        with socketserver.TCPServer(("", PORT), FractalHandler) as httpd:
            print(f"🚀 Fractal Explorer running at: http://localhost:{PORT}")
            print("Press Ctrl+C to stop the server.")
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        sys.exit(0)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
