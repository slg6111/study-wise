#lang racket
;; StudyWise functional classification component.
;; Pure functions: inputs are transformed into a classification and recommendation.
(provide classify-usage recommendation-for)

(define (classify-usage attempted ai-uses total)
  (cond
    [(or (not (exact-nonnegative-integer? attempted))
         (not (exact-nonnegative-integer? ai-uses))
         (not (exact-positive-integer? total))
         (> attempted total))
     "Invalid input"]
    [(and (< (* attempted 2) total)
          (>= (* ai-uses 2) total))
     "Overdependent"]
    [(= ai-uses 0)
     "Independent"]
    [else
     "Balanced"]))

(define (recommendation-for classification)
  (cond
    [(equal? classification "Overdependent")
     "Try each problem independently before asking AI. When stuck, request a hint rather than a complete solution."]
    [(equal? classification "Independent")
     "You are practicing without AI in this session. Keep checking your understanding and ask for explanations when genuinely needed."]
    [(equal? classification "Balanced")
     "You are combining independent attempts with AI support. Keep AI in a supporting role and verify its explanations."]
    [else
     "Check the input values and try again."]))

;; Command-line interface used by the C++ backend.
;; Arguments: attempted, ai-uses, total. Output: two lines for C++ to read.
(module+ main
  (define args (current-command-line-arguments))
  (unless (= (vector-length args) 3)
    (eprintf "Usage: racket classifier.rkt attempted ai-uses total\n")
    (exit 2))
  (define attempted (string->number (vector-ref args 0)))
  (define ai-uses (string->number (vector-ref args 1)))
  (define total (string->number (vector-ref args 2)))
  (define label (classify-usage attempted ai-uses total))
  (displayln label)
  (displayln (recommendation-for label)))
