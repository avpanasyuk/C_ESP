#!/usr/bin/env python3
"""
Simple HTTP server to receive POST data from ESP32
Parses comma-separated data: filename,data1,data2,...
Writes to CSV file with timestamp as first column
"""

import argparse
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime
from pathlib import Path


class ESP32DataHandler(BaseHTTPRequestHandler):
    """HTTP request handler for ESP32 data"""
    
    # Class variable to store log file path
    log_dir = None
    
    def do_POST(self):
        """Handle POST requests"""
        try:
            # Get content length
            content_length = int(self.headers.get('Content-Length', 0))
            
            # Read the request body
            body = self.rfile.read(content_length)
            data = body.decode('utf-8').strip()
            
            # Create timestamp
            timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            
            # Parse comma-separated data
            parts = [p.strip() for p in data.split(',')]
            
            if len(parts) < 1:
                print(f"\n[{timestamp}] Error: Empty data received")
                self.send_response(400)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(b'Error: Empty data')
                return
            
            # First element is filename
            filename = Path(self.log_dir) / parts[0]
            # Remaining elements are data
            csv_data = parts[1:]
            
            # Display to console
            print(f"\n[{timestamp}] Received data:")
            print(f"  File: {filename}")
            print(f"  Data: {', '.join(csv_data)}")
            print(f"  Size: {len(data)} bytes")
            
            # Write to CSV file
            try:
                csv_path = Path(filename)
                # Create parent directory if needed
                csv_path.parent.mkdir(parents=True, exist_ok=True)
                
                with open(csv_path, 'a') as f:
                    # Write timestamp as first column, then data
                    row = [timestamp] + csv_data
                    f.write(','.join(row) + '\n')
                
                print(f"  ✓ Written to: {csv_path.absolute()}")
            except IOError as e:
                print(f"  ✗ Error writing to CSV: {e}")
            
            # Send HTTP 200 response
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', '2')
            self.end_headers()
            self.wfile.write(b'OK')
            
        except Exception as e:
            print(f"Error processing request: {e}")
            self.send_response(500)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'Error')
    
    def log_message(self, format, *args):
        """Override to customize logging"""
        # Suppress default HTTP logging; we handle it ourselves
        pass


def main():
    parser = argparse.ArgumentParser(
        description='HTTP server to receive and log ESP32 POST data to CSV',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Data format: filename,data1,data2,data3,...
  - First element: CSV filename (created if doesn't exist)
  - Remaining elements: CSV data columns
  - Timestamp added as first column automatically

Examples:
  python http_server.py -p 8080
  python http_server.py -p 8080 -D /tmp
  python http_server.py -H 0.0.0.0 -p 5000 -D /tmp

ESP32 example:
  HTTP_POST_puts("http://192.168.1.100:8080", "sensor_data.csv,25.5,60.2,1013.25");
        '''
    )
    
    parser.add_argument(
        '-p', '--port',
        type=int,
        default=8000,
        help='Port to listen on (default: 8000)'
    )
    parser.add_argument(
        '-H', '--host',
        default='0.0.0.0',
        help='Host to bind to (default: localhost, use 0.0.0.0 for all interfaces)'
    )
    parser.add_argument(
        '-D', '--dir',
        default='/mnt/T',
        help='Directory to create log file in'
    )
    
    args = parser.parse_args()
    
    ESP32DataHandler.log_dir = args.dir;
    
    # Create and start server
    server_address = (args.host, args.port)
    httpd = HTTPServer(server_address, ESP32DataHandler)
    
    print(f"HTTP Server listening on http://{args.host}:{args.port}")
    print("Press Ctrl+C to stop\n")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n\nServer stopped")
        httpd.server_close()


if __name__ == '__main__':
    main()
