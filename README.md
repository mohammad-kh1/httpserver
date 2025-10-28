Simple C HTTP Server DevelopmentThis project is a step-by-step implementation of a modern HTTP server in C.Project Structuresrc/: Contains all C source files (currently main.c).build/: The directory where object files and the final executable are placed.Makefile: Used for compiling and cleaning the project.Building and RunningBuild: Compile the project using the provided Makefile:make
This creates the executable at build/httpserver.Run: Execute the server, specifying a port (e.g., 8080):make run
# OR: ./build/httpserver 8080
Test: Open your web browser or use curl to send a request. The server console will print the requested path.curl http://localhost:8080/users/profile.json
