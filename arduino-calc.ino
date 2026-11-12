#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// --------------------------------------------------
// OLED CONFIGURATION
// --------------------------------------------------

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --------------------------------------------------
// CALCULATOR MEMORY
// --------------------------------------------------
// On an Uno (2 KB SRAM) the display needs a 1 KB frame buffer
// allocated inside begin(), so everything else must stay small.
//
// The expression is evaluated directly with two stacks
// (shunting-yard), so no postfix array is needed.
//
// If "Free RAM after init" in the Serial Monitor is above ~200,
// you can raise MAX_EXPR / MAX_OPS / MAX_VALUES a little.

#define MAX_EXPR   32   // max 31 characters of input
#define MAX_OPS    32   // operator stack depth
#define MAX_VALUES 17   // 31 chars hold at most 16 numbers

char expression[MAX_EXPR];
char resultText[16];

int exprLength = 0;

// Operator stack symbols:
// '+', '-', '*', '/' = binary operators
// '~' = unary minus
// '(' = open parenthesis

char  operatorStack[MAX_OPS];
float valueStack[MAX_VALUES];

int operatorCount = 0;
int valueCount = 0;

// --------------------------------------------------
// FREE RAM (diagnostic, AVR only)
// --------------------------------------------------

#ifdef __AVR__
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start
                                  : (int)__brkval);
}
#endif

// --------------------------------------------------
// OLED DISPLAY
// --------------------------------------------------

void drawScreen(const char* expr, const char* result,
                const __FlashStringHelper* status) {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  int len = strlen(expr);

  // Show the last 21 characters if expression is long.
  int start = (len > 21) ? len - 21 : 0;

  display.setCursor(0, 0);
  display.print(expr + start);

  display.setCursor(0, 20);
  display.print(status);

  if (result != NULL && result[0] != '\0') {
    int resultLength = strlen(result);

    if (resultLength <= 9) {
      display.setTextSize(2);
      display.setCursor(0, 36);
    } else {
      display.setTextSize(1);
      display.setCursor(0, 40);
    }

    display.print(result);
  }

  display.display();
}

// --------------------------------------------------
// OPERATOR PRECEDENCE
// --------------------------------------------------

int precedence(char op) {

  if (op == '~') return 3;

  if (op == '*' || op == '/') return 2;

  if (op == '+' || op == '-') return 1;

  return 0;
}

// --------------------------------------------------
// APPLY ONE OPERATOR TO THE VALUE STACK
// Returns NULL on success, or an error message in flash.
// --------------------------------------------------

const __FlashStringHelper* applyOperator(char op) {

  // Unary minus.
  if (op == '~') {

    if (valueCount < 1) return F("SYNTAX ERROR");

    valueStack[valueCount - 1] = -valueStack[valueCount - 1];

    return NULL;
  }

  // Binary operators.
  if (valueCount < 2) return F("SYNTAX ERROR");

  float b = valueStack[--valueCount];
  float a = valueStack[--valueCount];

  float result = 0;

  switch (op) {

    case '+':
      result = a + b;
      break;

    case '-':
      result = a - b;
      break;

    case '*':
      result = a * b;
      break;

    case '/':
      if (b == 0.0f) return F("DIVISION BY ZERO");

      result = a / b;
      break;

    default:
      return F("INVALID OPERATOR");
  }

  if (isnan(result) || isinf(result)) return F("OVERFLOW");

  valueStack[valueCount++] = result;

  return NULL;
}

// --------------------------------------------------
// EVALUATE EXPRESSION (shunting-yard, no postfix array)
// Returns NULL on success, or an error message in flash.
// --------------------------------------------------

const __FlashStringHelper* evaluateExpression(const char* expr,
                                              float& answer) {

  operatorCount = 0;
  valueCount = 0;

  bool expectOperand = true;

  const __FlashStringHelper* error = NULL;

  int i = 0;

  while (expr[i] != '\0') {

    char c = expr[i];

    // Ignore spaces.
    if (c == ' ' || c == '\t') {
      i++;
      continue;
    }

    // ----------------------------------------------
    // NUMBER
    // ----------------------------------------------

    if ((c >= '0' && c <= '9') || c == '.') {

      if (!expectOperand) return F("SYNTAX ERROR");

      float number = 0;
      float decimalPlace = 0.1f;

      bool decimalFound = false;
      bool digitFound = false;

      while ((expr[i] >= '0' && expr[i] <= '9') ||
             expr[i] == '.') {

        if (expr[i] == '.') {

          if (decimalFound) return F("SYNTAX ERROR");

          decimalFound = true;

        } else {

          digitFound = true;

          int digit = expr[i] - '0';

          if (!decimalFound) {
            number = number * 10.0f + digit;
          } else {
            number += digit * decimalPlace;
            decimalPlace *= 0.1f;
          }
        }

        i++;
      }

      if (!digitFound) return F("SYNTAX ERROR");

      if (valueCount >= MAX_VALUES) return F("MEMORY ERROR");

      valueStack[valueCount++] = number;

      expectOperand = false;

      continue;
    }

    // ----------------------------------------------
    // OPENING PARENTHESIS
    // ----------------------------------------------

    if (c == '(') {

      if (!expectOperand) return F("SYNTAX ERROR");

      if (operatorCount >= MAX_OPS) return F("MEMORY ERROR");

      operatorStack[operatorCount++] = '(';

      i++;
      expectOperand = true;

      continue;
    }

    // ----------------------------------------------
    // CLOSING PARENTHESIS
    // ----------------------------------------------

    if (c == ')') {

      if (expectOperand) return F("SYNTAX ERROR");

      bool foundOpening = false;

      while (operatorCount > 0) {

        char op = operatorStack[--operatorCount];

        if (op == '(') {
          foundOpening = true;
          break;
        }

        error = applyOperator(op);
        if (error != NULL) return error;
      }

      if (!foundOpening) return F("SYNTAX ERROR");

      // Apply unary minus that sits outside the parentheses.
      if (operatorCount > 0 &&
          operatorStack[operatorCount - 1] == '~') {

        operatorCount--;

        error = applyOperator('~');
        if (error != NULL) return error;
      }

      i++;
      expectOperand = false;

      continue;
    }

    // ----------------------------------------------
    // OPERATORS
    // ----------------------------------------------

    if (c == 'x' || c == 'X') {
      c = '*';
    }

    if (c == '+' || c == '-' ||
        c == '*' || c == '/') {

      // Unary plus: ignore it.
      if (expectOperand && c == '+') {
        i++;
        continue;
      }

      // Unary minus.
      if (expectOperand && c == '-') {

        if (operatorCount >= MAX_OPS) return F("MEMORY ERROR");

        operatorStack[operatorCount++] = '~';

        i++;
        continue;
      }

      if (expectOperand) return F("SYNTAX ERROR");

      int currentPrecedence = precedence(c);

      // Apply operators with higher or equal precedence.
      while (operatorCount > 0) {

        char top = operatorStack[operatorCount - 1];

        if (top == '(') break;

        if (precedence(top) < currentPrecedence) break;

        operatorCount--;

        error = applyOperator(top);
        if (error != NULL) return error;
      }

      if (operatorCount >= MAX_OPS) return F("MEMORY ERROR");

      operatorStack[operatorCount++] = c;

      i++;
      expectOperand = true;

      continue;
    }

    // Unknown character.
    return F("SYNTAX ERROR");
  }

  // Expression ended while an operand was still expected
  // (also covers an empty expression).
  if (expectOperand) return F("SYNTAX ERROR");

  // Apply remaining operators.
  while (operatorCount > 0) {

    char op = operatorStack[--operatorCount];

    if (op == '(') return F("SYNTAX ERROR");

    error = applyOperator(op);
    if (error != NULL) return error;
  }

  if (valueCount != 1) return F("SYNTAX ERROR");

  answer = valueStack[0];

  return NULL;
}

// --------------------------------------------------
// FORMAT RESULT
// --------------------------------------------------

void formatResult(float value) {

  if (fabs(value) < 0.00005f) {
    value = 0;
  }

  // Keep output within a practical display range.
  if (fabs(value) > 9999999.0f) {
    strcpy_P(resultText, PSTR("OUT OF RANGE"));
    return;
  }

  dtostrf(value, 1, 4, resultText);

  // Remove trailing zeroes.
  int len = strlen(resultText);

  while (len > 0 && resultText[len - 1] == '0') {
    resultText[--len] = '\0';
  }

  // Remove trailing decimal point.
  if (len > 0 && resultText[len - 1] == '.') {
    resultText[--len] = '\0';
  }

  // Remove leading spaces.
  while (resultText[0] == ' ') {
    memmove(resultText, resultText + 1, strlen(resultText));
  }
}

// --------------------------------------------------
// CALCULATE
// --------------------------------------------------

void calculate() {

  expression[exprLength] = '\0';

  if (exprLength == 0) return;

  // Clear command.
  if (exprLength == 1 &&
      (expression[0] == 'C' || expression[0] == 'c')) {

    exprLength = 0;
    expression[0] = '\0';

    drawScreen("", "", F("READY"));

    return;
  }

  float answer = 0;

  const __FlashStringHelper* error =
    evaluateExpression(expression, answer);

  if (error != NULL) {

    drawScreen(expression, "", error);

    Serial.print(F("Error: "));
    Serial.println(error);

    exprLength = 0;
    expression[0] = '\0';

    return;
  }

  formatResult(answer);

  drawScreen(expression, resultText, F("RESULT:"));

  Serial.print(F("= "));
  Serial.println(resultText);

  exprLength = 0;
  expression[0] = '\0';
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {

  Serial.begin(9600);

  // Same initialisation as the working OLED test.
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {

    Serial.println(F("OLED init failed"));

#ifdef __AVR__
    Serial.print(F("Free RAM: "));
    Serial.println(freeRam());
#endif

    while (true);
  }

  display.clearDisplay();
  display.display();

  expression[0] = '\0';

  drawScreen("", "", F("READY"));

  Serial.println(F("Ready. Expression + Enter, C = clear."));

#ifdef __AVR__
  Serial.print(F("Free RAM after init: "));
  Serial.println(freeRam());
#endif
}

// --------------------------------------------------
// MAIN LOOP
// --------------------------------------------------

void loop() {

  while (Serial.available() > 0) {

    char c = Serial.read();

    // Ignore carriage return.
    if (c == '\r') {
      continue;
    }

    // ENTER: calculate.
    if (c == '\n') {

      calculate();

      continue;
    }

    // BACKSPACE.
    if (c == 8 || c == 127) {

      if (exprLength > 0) {

        exprLength--;

        expression[exprLength] = '\0';

        drawScreen(expression, "", F("ENTER EXPRESSION"));
      }

      continue;
    }

    // Ignore unsupported control characters.
    if (c < 32 || c > 126) {
      continue;
    }

    // Input buffer protection.
    if (exprLength >= MAX_EXPR - 1) {

      Serial.println();
      Serial.println(F("Error: Expression too long."));

      drawScreen(expression, "", F("INPUT TOO LONG"));

      exprLength = 0;
      expression[0] = '\0';

      continue;
    }

    // Store input.
    expression[exprLength++] = c;
    expression[exprLength] = '\0';

    drawScreen(expression, "", F("ENTER EXPRESSION"));
  }
}
