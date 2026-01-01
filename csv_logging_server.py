#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime
import os

LOG_DIR = "/tmp/logs"

# Create logs directory if it doesn't exist
os.makedirs(LOG_DIR, exist_ok=True)

class RequestHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8')
        
        if body:
            # Parse CSV: first field is log filename, rest is data
            fields = body.split(',')
            if len(fields) >= 1:
                log_name = fields[0]
                data_fields = ','.join(fields[1:])  # Rejoin remaining fields
                
                # Print to console
                timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                print(f"[{timestamp}] [{log_name}] {data_fields}")
                
                # Log to disk in logs/{filename}.log
                log_file = os.path.join(LOG_DIR, f"{log_name}.log")
                with open(log_file, 'a') as f:
                    f.write(f"[{timestamp}] {data_fields}\n")
                
                self.send_response(200)
                self.end_headers()
            else:
                self.send_response(400)
                self.end_headers()
        else:
            self.send_response(400)
            self.end_headers()
    
    def log_message(self, format, *args):
        pass  # Suppress default logging

if __name__ == '__main__':
    httpd = HTTPServer(('0.0.0.0', 8000), RequestHandler)
    print("Server listening on port 8000...")
    print(f"Log files will be saved to {LOG_DIR}/")
    httpd.serve_forever()
