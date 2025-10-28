Simple C HTTP ServerThis project is a step-by-step implementation of a basic, concurrent HTTP server in C.Project Structure.
├── LICENSE
├── README.md
├── src
│   └── main.c
├── test
└── webroot  <-- NEW: This directory will contain all files served by the server (e.g., index.html)
Build and RunBuild:make build
Create the webroot directory and a test file (MANDATORY for Step 5):mkdir webroot
echo "<h1>It works!</h1><p>This is the test index file.</p>" > webroot/index.html
Run: (Runs the server on port 8080)make run
Access: Open your browser to http://localhost:8080/index.html.Implemented StepsExtract URL pathRespond with body (Now handles file content)Read headerConcurrent connectionsReturn a file (Implemented)
