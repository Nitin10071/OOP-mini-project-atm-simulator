/*
 * ATM Simulator - OOP Mini Project (C++)
 * ======================================
 * Everything lives inside classes. The only code outside a class is main(),
 * which C++ requires as the program entry point; it just starts Application.
 *
 * OOP concepts used
 * -----------------
 * Abstraction   : Account is an abstract class (pure virtual functions)
 * Inheritance   : SavingsAccount and CurrentAccount derive from Account
 * Polymorphism  : virtual functions - each account type has its own rules
 * Encapsulation : private PIN hash, read-only balance, access via methods
 * Composition   : Bank owns Accounts, Account owns Transactions, ATM uses Bank
 * Exceptions    : custom exception class hierarchy
 *
 * Compile : g++ -std=c++11 -Wall -o atm atm_simulator.cpp
 * Run     : ./atm        (Windows: atm.exe)
 */

#define _CRT_SECURE_NO_WARNINGS  // silences localtime() warnings in Visual Studio

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================ FORMAT (helper)
class Format {
public:
    // 25000 -> "25,000.00"
    static std::string number(double value) {
        std::ostringstream raw;
        raw << std::fixed << std::setprecision(2) << std::fabs(value);
        std::string text = raw.str();
        std::size_t dot = text.find('.');
        std::string whole = text.substr(0, dot);
        std::string fraction = text.substr(dot);
        for (int i = static_cast<int>(whole.size()) - 3; i > 0; i -= 3) {
            whole.insert(static_cast<std::size_t>(i), ",");
        }
        bool negative = value < 0 && std::fabs(value) >= 0.005;
        return (negative ? "-" : "") + whole + fraction;
    }

    static std::string money(double value) { return "Rs. " + number(value); }

    static std::string upper(std::string text) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            text[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        }
        return text;
    }
};

// ============================================================ EXCEPTIONS
class ATMError : public std::runtime_error {
public:
    explicit ATMError(const std::string& message) : std::runtime_error(message) {}
};

class InvalidAmountError : public ATMError {
public:
    explicit InvalidAmountError(const std::string& m) : ATMError(m) {}
};

class InsufficientFundsError : public ATMError {
public:
    explicit InsufficientFundsError(const std::string& m) : ATMError(m) {}
};

class LimitExceededError : public ATMError {
public:
    explicit LimitExceededError(const std::string& m) : ATMError(m) {}
};

class AuthenticationError : public ATMError {
public:
    explicit AuthenticationError(const std::string& m) : ATMError(m) {}
};

class AccountLockedError : public ATMError {
public:
    explicit AccountLockedError(const std::string& m) : ATMError(m) {}
};

class AccountNotFoundError : public ATMError {
public:
    explicit AccountNotFoundError(const std::string& m) : ATMError(m) {}
};

class InvalidPinError : public ATMError {
public:
    explicit InvalidPinError(const std::string& m) : ATMError(m) {}
};

// Raised when the input stream closes (Ctrl+D / Ctrl+Z); not a banking error.
class InputClosedError : public std::runtime_error {
public:
    InputClosedError() : std::runtime_error("input closed") {}
};

// ============================================================ TRANSACTION
enum class TransactionType { Opening, Deposit, Withdrawal, TransferOut, TransferIn };

class Transaction {
public:
    Transaction(TransactionType type, double amount, double balanceAfter,
                const std::string& note = "")
        : type_(type),
          amount_(amount),  // positive = credit, negative = debit
          balanceAfter_(balanceAfter),
          note_(note),
          timestamp_(std::time(nullptr)) {}

    TransactionType type() const { return type_; }
    double amount() const { return amount_; }
    double balanceAfter() const { return balanceAfter_; }
    const std::string& note() const { return note_; }

    std::string dateKey() const { return keyFor(timestamp_); }
    static std::string todayKey() { return keyFor(std::time(nullptr)); }

    static const char* label(TransactionType type) {
        switch (type) {
            case TransactionType::Opening:     return "Opening Balance";
            case TransactionType::Deposit:     return "Deposit";
            case TransactionType::Withdrawal:  return "Withdrawal";
            case TransactionType::TransferOut: return "Transfer Out";
            case TransactionType::TransferIn:  return "Transfer In";
        }
        return "";
    }

    // One formatted statement line
    std::string toString() const {
        std::ostringstream out;
        out << timeText() << "  " << std::left << std::setw(16) << label(type_)
            << (amount_ >= 0 ? '+' : '-') << std::right << std::setw(11)
            << Format::number(std::fabs(amount_)) << "  " << std::setw(12)
            << Format::number(balanceAfter_);
        return out.str();
    }

private:
    static std::string keyFor(std::time_t t) {
        char buffer[16];
        std::tm local = *std::localtime(&t);
        std::strftime(buffer, sizeof(buffer), "%Y%m%d", &local);
        return buffer;
    }

    std::string timeText() const {
        char buffer[32];
        std::tm local = *std::localtime(&timestamp_);
        std::strftime(buffer, sizeof(buffer), "%d-%b-%y %H:%M", &local);
        return buffer;
    }

    TransactionType type_;
    double amount_;
    double balanceAfter_;
    std::string note_;
    std::time_t timestamp_;
};

// ============================================================ ACCOUNT (abstract)
class Account {
public:
    static constexpr int MAX_PIN_ATTEMPTS = 3;

    Account(std::string accountNumber, std::string holderName, const std::string& pin,
            double openingBalance = 0.0)
        : balance_(0.0),
          accountNumber_(std::move(accountNumber)),
          holderName_(std::move(holderName)),
          pinHash_(0),
          failedAttempts_(0),
          locked_(false) {
        validatePinFormat(pin);
        if (openingBalance < 0) {
            throw InvalidAmountError("Opening balance cannot be negative.");
        }
        pinHash_ = hashPin(pin);
        if (openingBalance > 0) {
            apply(openingBalance);
            record(TransactionType::Opening, openingBalance);
        }
    }

    virtual ~Account() {}

    // an account has a unique identity, so copying it makes no sense
    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;

    // ---------- abstract interface: every account type MUST provide these
    virtual std::string accountType() const = 0;
    virtual double dailyLimit() const = 0;
    virtual std::string rulesSummary() const = 0;

    // ---------- read-only access (encapsulation)
    const std::string& accountNumber() const { return accountNumber_; }
    const std::string& holderName() const { return holderName_; }
    double balance() const { return balance_; }
    bool isLocked() const { return locked_; }

    std::string toString() const {
        return accountType() + " A/c " + accountNumber_ + " (" + holderName_ + ")";
    }

    // ---------- security
    bool verifyPin(const std::string& pin) {
        if (locked_) {
            throw AccountLockedError("This card is blocked. Please contact your bank.");
        }
        if (hashPin(pin) == pinHash_) {
            failedAttempts_ = 0;
            return true;
        }
        ++failedAttempts_;
        int remaining = MAX_PIN_ATTEMPTS - failedAttempts_;
        if (remaining <= 0) {
            locked_ = true;
            throw AccountLockedError("Too many wrong PINs. Card has been blocked.");
        }
        throw AuthenticationError("Incorrect PIN. " + std::to_string(remaining) +
                                  " attempt(s) left.");
    }

    void changePin(const std::string& oldPin, const std::string& newPin) {
        verifyPin(oldPin);
        validatePinFormat(newPin);
        if (hashPin(newPin) == pinHash_) {
            throw InvalidPinError("New PIN must be different from the current PIN.");
        }
        pinHash_ = hashPin(newPin);
    }

    // ---------- banking operations
    void deposit(double amount) {
        validateAmount(amount);
        apply(amount);
        record(TransactionType::Deposit, amount);
    }

    void withdraw(double amount) {
        validateAmount(amount);
        checkDailyLimit(amount);
        checkBalanceRule(amount);  // polymorphic call
        apply(-amount);
        record(TransactionType::Withdrawal, -amount);
    }

    void sendTransfer(double amount, const std::string& toNumber) {
        validateAmount(amount);
        checkBalanceRule(amount);  // polymorphic call
        apply(-amount);
        record(TransactionType::TransferOut, -amount, "to " + toNumber);
    }

    void receiveTransfer(double amount, const std::string& fromNumber) {
        validateAmount(amount);
        apply(amount);
        record(TransactionType::TransferIn, amount, "from " + fromNumber);
    }

    std::vector<Transaction> miniStatement(std::size_t count = 5) const {
        std::size_t start = transactions_.size() > count ? transactions_.size() - count : 0;
        return std::vector<Transaction>(transactions_.begin() + static_cast<long>(start),
                                        transactions_.end());
    }

protected:
    // each subclass decides when a debit is NOT allowed
    virtual void checkBalanceRule(double amount) const = 0;

    double balance_;                         // subclasses need it for their rules
    std::vector<Transaction> transactions_;

private:
    void apply(double delta) { balance_ = std::round((balance_ + delta) * 100.0) / 100.0; }

    void record(TransactionType type, double signedAmount, const std::string& note = "") {
        transactions_.push_back(Transaction(type, signedAmount, balance_, note));
    }

    double withdrawnToday() const {
        const std::string today = Transaction::todayKey();
        double total = 0.0;
        for (std::size_t i = 0; i < transactions_.size(); ++i) {
            const Transaction& t = transactions_[i];
            if (t.type() == TransactionType::Withdrawal && t.dateKey() == today) {
                total += -t.amount();
            }
        }
        return total;
    }

    void checkDailyLimit(double amount) const {
        double remaining = dailyLimit() - withdrawnToday();
        if (amount > remaining) {
            throw LimitExceededError("Daily limit exceeded. You can withdraw " +
                                     Format::money(remaining) + " more today.");
        }
    }

    static void validateAmount(double amount) {
        if (!std::isfinite(amount) || amount <= 0) {
            throw InvalidAmountError("Amount must be a positive number.");
        }
    }

    static void validatePinFormat(const std::string& pin) {
        bool ok = pin.size() == 4;
        for (std::size_t i = 0; ok && i < pin.size(); ++i) {
            ok = std::isdigit(static_cast<unsigned char>(pin[i])) != 0;
        }
        if (!ok) {
            throw InvalidPinError("PIN must be exactly 4 digits.");
        }
    }

    static std::size_t hashPin(const std::string& pin) { return std::hash<std::string>()(pin); }

    // private data - hidden from outside code AND from subclasses
    std::string accountNumber_;
    std::string holderName_;
    std::size_t pinHash_;
    int failedAttempts_;
    bool locked_;
};

// ------------------------------------------------------------ SavingsAccount
class SavingsAccount : public Account {
public:
    static constexpr double MIN_BALANCE = 500.0;

    SavingsAccount(const std::string& number, const std::string& holder,
                   const std::string& pin, double openingBalance = 0.0)
        : Account(number, holder, pin, openingBalance) {}

    std::string accountType() const override { return "Savings"; }
    double dailyLimit() const override { return 20000.0; }

    std::string rulesSummary() const override {
        return "Min balance Rs. 500, daily limit Rs. 20,000";
    }

protected:
    void checkBalanceRule(double amount) const override {
        if (balance_ - amount < MIN_BALANCE) {
            double usable = balance_ > MIN_BALANCE ? balance_ - MIN_BALANCE : 0.0;
            throw InsufficientFundsError("Insufficient funds. Minimum balance of " +
                                         Format::money(MIN_BALANCE) +
                                         " must be maintained (usable: " +
                                         Format::money(usable) + ").");
        }
    }
};

// ------------------------------------------------------------ CurrentAccount
class CurrentAccount : public Account {
public:
    static constexpr double OVERDRAFT_LIMIT = 10000.0;

    CurrentAccount(const std::string& number, const std::string& holder,
                   const std::string& pin, double openingBalance = 0.0)
        : Account(number, holder, pin, openingBalance) {}

    std::string accountType() const override { return "Current"; }
    double dailyLimit() const override { return 50000.0; }

    std::string rulesSummary() const override {
        return "Overdraft up to Rs. 10,000, daily limit Rs. 50,000";
    }

protected:
    void checkBalanceRule(double amount) const override {
        if (balance_ - amount < -OVERDRAFT_LIMIT) {
            throw InsufficientFundsError("Overdraft limit of " + Format::money(OVERDRAFT_LIMIT) +
                                         " exceeded (usable: " +
                                         Format::money(balance_ + OVERDRAFT_LIMIT) + ").");
        }
    }
};

// ============================================================ BANK
class Bank {
public:
    explicit Bank(std::string name) : name_(std::move(name)), nextNumber_(1001) {}

    const std::string& name() const { return name_; }

    // Bank creates and OWNS its accounts (unique_ptr frees them automatically)
    template <typename AccountType>
    Account& openAccount(const std::string& holderName, const std::string& pin,
                         double openingBalance = 0.0) {
        static_assert(std::is_base_of<Account, AccountType>::value,
                      "AccountType must derive from Account");
        std::string number = std::to_string(nextNumber_++);
        accounts_[number] = std::unique_ptr<Account>(
            new AccountType(number, holderName, pin, openingBalance));
        return *accounts_[number];
    }

    Account& findAccount(const std::string& number) {
        std::map<std::string, std::unique_ptr<Account> >::iterator it = accounts_.find(number);
        if (it == accounts_.end()) {
            throw AccountNotFoundError("Account '" + number + "' not found.");
        }
        return *it->second;
    }

    Account& transfer(Account& source, const std::string& targetNumber, double amount) {
        Account& target = findAccount(targetNumber);
        if (&target == &source) {
            throw ATMError("You cannot transfer money to your own account.");
        }
        source.sendTransfer(amount, target.accountNumber());  // may throw: nothing changes
        target.receiveTransfer(amount, source.accountNumber());
        return target;
    }

private:
    std::string name_;
    int nextNumber_;
    std::map<std::string, std::unique_ptr<Account> > accounts_;
};

// ============================================================ CONSOLE (UI helper)
class Console {
public:
    static const int WIDTH = 54;

    static void line(char fill = '-') { std::cout << std::string(WIDTH, fill) << "\n"; }

    static void header(const std::string& title) {
        std::cout << "\n";
        line('=');
        int pad = (WIDTH - static_cast<int>(title.size())) / 2;
        std::cout << std::string(pad > 0 ? pad : 0, ' ') << title << "\n";
        line('=');
    }

    static std::string ask(const std::string& prompt) {
        std::cout << prompt << std::flush;
        std::string text;
        if (!std::getline(std::cin, text)) {
            throw InputClosedError();
        }
        std::size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        std::size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    static void error(const std::string& message) { std::cout << "\n  [!] " << message << "\n"; }
    static void success(const std::string& message) { std::cout << "\n  [OK] " << message << "\n"; }
};

// ============================================================ ATM (controller)
class ATM {
public:
    explicit ATM(Bank& bank, double cashAvailable = 200000.0)
        : bank_(bank), cash_(cashAvailable), account_(nullptr) {}

    void run() {
        try {
            while (true) {
                Console::header("WELCOME TO " + Format::upper(bank_.name()) + " ATM");
                std::cout << "  1. Insert card (login)\n  2. Exit\n";
                std::string choice = Console::ask("\n  Select option: ");
                if (choice == "1") {
                    startSession();
                } else if (choice == "2") {
                    std::cout << "\n  Thank you for banking with us. Goodbye!\n";
                    return;
                } else {
                    Console::error("Invalid option.");
                }
            }
        } catch (const InputClosedError&) {
            std::cout << "\n\n  Machine switched off. Goodbye!\n";
        }
    }

private:
    static constexpr double CASH_MULTIPLE = 100.0;

    // ---------- session handling
    void startSession() {
        std::string number = Console::ask("\n  Enter account number: ");
        try {
            account_ = authenticate(number);
        } catch (const ATMError& err) {
            Console::error(err.what());
            return;
        }
        Console::success("Welcome, " + account_->holderName() + "!");
        sessionMenu();
        account_ = nullptr;
        std::cout << "\n  Session ended. Please take your card.\n";
    }

    Account* authenticate(const std::string& number) {
        Account& account = bank_.findAccount(number);
        while (true) {  // ends by success or by AccountLockedError
            std::string pin = Console::ask("  Enter 4-digit PIN: ");
            try {
                account.verifyPin(pin);
                return &account;
            } catch (const AuthenticationError& err) {
                Console::error(err.what());
            }
        }
    }

    void printMenu() {
        Console::header("MAIN MENU");
        std::cout << "  1. Balance enquiry\n"
                  << "  2. Deposit cash\n"
                  << "  3. Withdraw cash\n"
                  << "  4. Transfer money\n"
                  << "  5. Mini statement\n"
                  << "  6. Change PIN\n"
                  << "  7. Exit\n";
    }

    void sessionMenu() {
        while (true) {
            printMenu();
            std::string choice = Console::ask("\n  Select option: ");
            if (choice == "7") return;

            try {
                if (choice == "1")      balanceEnquiry();
                else if (choice == "2") depositCash();
                else if (choice == "3") withdrawCash();
                else if (choice == "4") transferMoney();
                else if (choice == "5") miniStatement();
                else if (choice == "6") changePin();
                else {
                    Console::error("Invalid option.");
                    continue;
                }
            } catch (const ATMError& err) {
                Console::error(err.what());
            }

            if (account_->isLocked()) {  // e.g. wrong PINs while changing PIN
                Console::error("Card blocked. Ending session.");
                return;
            }
        }
    }

    // ---------- input helpers
    double readAmount(const std::string& prompt) {
        std::string text = Console::ask(prompt);
        double amount = 0.0;
        try {
            std::size_t used = 0;
            amount = std::stod(text, &used);
            if (used != text.size()) throw std::invalid_argument("trailing characters");
        } catch (const std::exception&) {
            throw InvalidAmountError("Please enter a valid numeric amount.");
        }
        if (!std::isfinite(amount) || amount <= 0) {
            throw InvalidAmountError("Amount must be a positive number.");
        }
        return amount;
    }

    double readCashAmount(const std::string& prompt) {
        double amount = readAmount(prompt);
        if (std::fmod(amount, CASH_MULTIPLE) != 0.0) {
            throw InvalidAmountError("Amount must be in multiples of Rs. 100.");
        }
        return amount;
    }

    // ---------- menu actions
    void balanceEnquiry() {
        Console::line();
        std::cout << "  Account : " << account_->toString() << "\n"
                  << "  Balance : " << Format::money(account_->balance()) << "\n"
                  << "  Rules   : " << account_->rulesSummary() << "\n";  // polymorphic call
        Console::line();
    }

    void depositCash() {
        double amount = readCashAmount("\n  Enter amount to deposit: ");
        account_->deposit(amount);
        cash_ += amount;
        Console::success(Format::money(amount) + " deposited.");
        std::cout << "  Available balance: " << Format::money(account_->balance()) << "\n";
    }

    void withdrawCash() {
        double amount = readCashAmount("\n  Enter amount to withdraw: ");
        if (amount > cash_) {
            throw ATMError("This ATM does not have enough cash. Try a smaller amount.");
        }
        account_->withdraw(amount);  // may throw: then no cash is dispensed
        cash_ -= amount;
        Console::success("Please collect your cash: " + Format::money(amount));
        std::cout << "  Available balance: " << Format::money(account_->balance()) << "\n";
    }

    void transferMoney() {
        std::string targetNumber = Console::ask("\n  Enter beneficiary account number: ");
        if (targetNumber == account_->accountNumber()) {
            throw ATMError("You cannot transfer money to your own account.");
        }
        Account& target = bank_.findAccount(targetNumber);
        double amount = readAmount("  Enter amount to transfer: ");

        std::string answer = Console::ask("  Transfer " + Format::money(amount) + " to " +
                                          target.holderName() + "? (y/n): ");
        if (answer != "y" && answer != "Y") {
            std::cout << "\n  Transfer cancelled.\n";
            return;
        }

        bank_.transfer(*account_, targetNumber, amount);
        Console::success(Format::money(amount) + " sent to " + target.holderName() + ".");
        std::cout << "  Available balance: " << Format::money(account_->balance()) << "\n";
    }

    void miniStatement() {
        std::vector<Transaction> entries = account_->miniStatement(5);
        Console::line();
        std::cout << "  Last transactions\n";
        Console::line();
        std::cout << "  " << std::left << std::setw(15) << "Date/Time" << "  "
                  << std::setw(16) << "Type" << std::right << std::setw(12) << "Amount"
                  << "  " << std::setw(12) << "Balance" << "\n";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::cout << "  " << entries[i].toString() << "\n";
        }
        if (entries.empty()) std::cout << "  No transactions yet.\n";
        Console::line();
    }

    void changePin() {
        std::string oldPin = Console::ask("\n  Enter current PIN: ");
        std::string newPin = Console::ask("  Enter new 4-digit PIN: ");
        std::string again = Console::ask("  Re-enter new PIN: ");
        if (newPin != again) {
            throw InvalidPinError("The two new PINs do not match.");
        }
        account_->changePin(oldPin, newPin);
        Console::success("PIN changed successfully.");
    }

    Bank& bank_;
    double cash_;
    Account* account_;  // account of the current session (not owned)
};

// ============================================================ APPLICATION
class Application {
public:
    static void run() {
        Bank bank("National Bank");
        Account& savings = bank.openAccount<SavingsAccount>("Nitin Kumar", "1234", 25000);
        Account& current = bank.openAccount<CurrentAccount>("Akhilesh Singh", "4321", 50000);

        std::cout << "\n  --- Demo cards (for testing) ---\n"
                  << "  " << savings.toString() << "  PIN: 1234\n"
                  << "  " << current.toString() << "  PIN: 4321\n";

        ATM atm(bank);
        atm.run();
    }
};

// C++ needs main() as the entry point - it only hands control to a class.
int main() {
    Application::run();
    return 0;
}
