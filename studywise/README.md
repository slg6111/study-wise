# StudyWise — C++ + Racket integrated prototype

## Requirements
- A C++ compiler (g++)
- Racket installed and available as the `racket` command
- macOS or Linux (this starter backend uses POSIX sockets and process APIs)

## Build and run
Open a terminal in the `studywise` project root (the folder containing `index.html`).

```bash
g++ -std=c++17 -Wall -Wextra -O2 backend/server.cpp -o studywise-server
./studywise-server
```

Then open **http://localhost:8080** in your browser. Do not open `index.html` directly: the page must be served by the C++ backend so its `/api/assess` request reaches the server.

If port 8080 is busy, choose another port:
```bash
./studywise-server 8090
```
The browser page and API will then be at `http://localhost:8090`.

## Test the API
In a second terminal:

```bash
curl http://localhost:8080/api/health

curl -X POST http://localhost:8080/api/assess \
  -H "Content-Type: application/json" \
  -d '{"attempted":2,"aiUses":5,"total":10}'
```

Expected assessment result includes:
```json
{
  "classification": "Overdependent",
  "recommendation": "Try each problem independently before asking AI. When stuck, request a hint rather than a complete solution.",
  "engine": "Racket"
}
```

Try other cases:
- `{"attempted":8,"aiUses":1,"total":10}` → `Balanced`
- `{"attempted":6,"aiUses":0,"total":10}` → `Independent`
- `{"attempted":11,"aiUses":2,"total":10}` → HTTP 400 validation error

## How the integration works
1. The browser sends JSON to `POST /api/assess`.
2. The C++ backend parses and validates the numeric fields.
3. The C++ `Assessment` class encapsulates assessment data and validation.
4. The imperative request handler controls the sequence of operations.
5. C++ starts `racket/classifier.rkt` as a child process and passes the values as command-line arguments.
6. The Racket program uses pure functions and returns the classification and recommendation.
7. C++ packages the result as JSON for the browser.
8. The frontend displays the result and stores the session in browser localStorage.

## Paradigm evidence
- **Imperative (C++):** request handling, validation flow, process invocation, and response construction.
- **Object-oriented (C++):** `Assessment` encapsulates fields and validation behavior.
- **Functional (Racket):** `classify-usage` and `recommendation-for` are functions that return results without mutating shared state.
- **Declarative (SQL):** not connected in this milestone; browser localStorage is still used for assessment history. SQLite is the next integration task.

## Important limitations
- This is a teaching prototype, not a hardened production web server. It is single-threaded, uses a minimal HTTP parser, and is intended for localhost demonstrations.
- The backend expects to be launched from the project root so it can find `index.html` and `racket/classifier.rkt`.
- Racket must be installed. If the `racket` executable is not found, the API returns HTTP 503.
- Assessment history is still stored in the browser, not in a shared database.
- The learning assistant remains a local rule-based demo. No external AI API is called yet.
- Do not expose this starter server directly to the public internet. Add authentication, robust HTTP/JSON libraries, TLS, rate limiting, and production-grade error handling before deployment.
