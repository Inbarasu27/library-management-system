#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOOK_FILE    "books.txt"
#define STUDENT_FILE "students.txt"
#define LOAN_FILE    "loans.txt"

#define RETURN_DAYS  10
#define FINE_PER_DAY 10

typedef struct
{
    int id;
    char title[100];
    char author[100];
    int copies;          /* total copies owned by the library */
} Book;

typedef struct
{
    int id;
    char name[100];
    char department[50];
} Student;

typedef struct
{
    int loan_id;
    int book_id;
    int student_id;
    time_t issue_date;
    time_t due_date;
    time_t return_date;
    int fine;
    int paid;
    int returned;
} Loan;

/* ---------- IN-MEMORY DATA ---------- */

static Book    *books    = NULL;
static Student *students = NULL;
static Loan    *loans    = NULL;

static int nbooks = 0,    capbooks = 0;
static int nstudents = 0, capstudents = 0;
static int nloans = 0,    caploans = 0;

static int dirty = 0;   /* 1 = there are unsaved changes */

int save_all(void);

static void *grow(void *arr, int count, int *cap, size_t size)
{
    if (count < *cap)
        return arr;

    int newcap = *cap ? *cap * 2 : 16;
    void *p = realloc(arr, (size_t)newcap * size);

    if (!p)
    {
        printf("Out of memory!\n");
        if (dirty) save_all();
        exit(1);
    }

    *cap = newcap;
    return p;
}


/* ---------- INPUT FUNCTIONS ---------- */

void read_text(char *prompt, char *value, int size)
{
    do
    {
        printf("%s", prompt);

        if (!fgets(value, size, stdin))
        {
            if (dirty) save_all();
            exit(1);
        }

        /* line too long: throw away the rest */
        if (!strchr(value, '\n'))
        {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }

        value[strcspn(value, "\r\n")] = '\0';

        /* '|' is used as the file separator */
        for (char *p = value; *p; p++)
            if (*p == '|') *p = '/';

        if (value[0] == '\0')
            printf("Cannot be empty!\n");

    } while (value[0] == '\0');
}

int read_int(char *prompt)
{
    char buffer[100];
    char *end;
    long value;

    while (1)
    {
        printf("%s", prompt);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            if (dirty) save_all();
            exit(1);
        }

        value = strtol(buffer, &end, 10);

        if (end != buffer && strspn(end, " \t\r\n") == strlen(end)
            && value >= 0 && value <= 2147483647L)
            return (int)value;

        printf("Invalid input! Enter a valid number.\n");
    }
}

/* ID must be 1 or more */
int read_id(char *prompt)
{
    int v;

    while ((v = read_int(prompt)) == 0)
        printf("ID must be greater than 0.\n");

    return v;
}


/* ---------- DATE FUNCTIONS ---------- */

time_t today(void)
{
    time_t now = time(NULL);
    struct tm date = *localtime(&now);

    date.tm_hour = 12;
    date.tm_min = 0;
    date.tm_sec = 0;
    date.tm_isdst = -1;

    return mktime(&date);
}

time_t calculate_due_date(time_t issue)
{
    struct tm date = *localtime(&issue);

    date.tm_mday += RETURN_DAYS;
    date.tm_isdst = -1;

    return mktime(&date);
}

void display_date(time_t date)
{
    struct tm *d = localtime(&date);

    printf("%02d-%02d-%04d",
           d->tm_mday, d->tm_mon + 1, d->tm_year + 1900);
}

/* Calendar-day number, independent of daylight-saving changes. */
long date_number(time_t value)
{
    struct tm d = *localtime(&value);

    long y = d.tm_year + 1900;
    long m = d.tm_mon + 1;
    long day = d.tm_mday;

    y -= (m <= 2);

    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe;
}


/* ---------- LOAD / SAVE ---------- */

static int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;

    for (char *p = line; *p && n < max; p++)
    {
        if (*p == '|')
        {
            *p = '\0';
            f[n++] = p + 1;
        }
    }
    return n;
}

static void load_books(void)
{
    FILE *fp = fopen(BOOK_FILE, "r");
    char line[512];
    char *f[4];
    int n;

    if (!fp) return;   /* first run: no file yet */

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';

        n = split_fields(line, f, 4);
        if (n != 3 && n != 4) continue;   /* 3 = old file without copies */

        books = grow(books, nbooks, &capbooks, sizeof(Book));

        Book *b = &books[nbooks];
        b->id = atoi(f[0]);
        snprintf(b->title, sizeof(b->title), "%s", f[1]);
        snprintf(b->author, sizeof(b->author), "%s", f[2]);
        b->copies = (n == 4) ? atoi(f[3]) : 1;
        if (b->copies < 1) b->copies = 1;
        nbooks++;
    }

    fclose(fp);
}

static void load_students(void)
{
    FILE *fp = fopen(STUDENT_FILE, "r");
    char line[512];
    char *f[3];

    if (!fp) return;

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (split_fields(line, f, 3) != 3) continue;

        students = grow(students, nstudents, &capstudents, sizeof(Student));

        Student *s = &students[nstudents];
        s->id = atoi(f[0]);
        snprintf(s->name, sizeof(s->name), "%s", f[1]);
        snprintf(s->department, sizeof(s->department), "%s", f[2]);
        nstudents++;
    }

    fclose(fp);
}

static void load_loans(void)
{
    FILE *fp = fopen(LOAN_FILE, "r");
    char line[512];
    char *f[9];

    if (!fp) return;

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (split_fields(line, f, 9) != 9) continue;

        loans = grow(loans, nloans, &caploans, sizeof(Loan));

        Loan *l = &loans[nloans];
        l->loan_id    = atoi(f[0]);
        l->book_id    = atoi(f[1]);
        l->student_id = atoi(f[2]);
        l->issue_date  = (time_t)strtoll(f[3], NULL, 10);
        l->due_date    = (time_t)strtoll(f[4], NULL, 10);
        l->return_date = (time_t)strtoll(f[5], NULL, 10);
        l->fine     = atoi(f[6]);
        l->paid     = atoi(f[7]);
        l->returned = atoi(f[8]);
        nloans++;
    }

    fclose(fp);
}

void load_all(void)
{
    load_books();
    load_students();
    load_loans();
}

/* Write to a temp file first, then replace the real file. */
static int replace_file(const char *tmp, const char *real)
{
    if (rename(tmp, real) == 0)
        return 1;

    /* Windows: rename fails if target exists */
    remove(real);
    return rename(tmp, real) == 0;
}

static int save_books(void)
{
    FILE *fp = fopen("books.tmp", "w");
    if (!fp) return 0;

    for (int i = 0; i < nbooks; i++)
        fprintf(fp, "%d|%s|%s|%d\n", books[i].id, books[i].title,
                books[i].author, books[i].copies);

    if (ferror(fp) | fclose(fp)) return 0;
    return replace_file("books.tmp", BOOK_FILE);
}

static int save_students(void)
{
    FILE *fp = fopen("students.tmp", "w");
    if (!fp) return 0;

    for (int i = 0; i < nstudents; i++)
        fprintf(fp, "%d|%s|%s\n", students[i].id, students[i].name,
                students[i].department);

    if (ferror(fp) | fclose(fp)) return 0;
    return replace_file("students.tmp", STUDENT_FILE);
}

static int save_loans(void)
{
    FILE *fp = fopen("loans.tmp", "w");
    if (!fp) return 0;

    for (int i = 0; i < nloans; i++)
    {
        Loan *l = &loans[i];
        fprintf(fp, "%d|%d|%d|%lld|%lld|%lld|%d|%d|%d\n",
                l->loan_id, l->book_id, l->student_id,
                (long long)l->issue_date, (long long)l->due_date,
                (long long)l->return_date,
                l->fine, l->paid, l->returned);
    }

    if (ferror(fp) | fclose(fp)) return 0;
    return replace_file("loans.tmp", LOAN_FILE);
}

int save_all(void)
{
    if (save_books() && save_students() && save_loans())
    {
        dirty = 0;
        return 1;
    }
    return 0;
}

void save_menu(void)
{
    if (save_all())
        printf("\nAll data saved successfully!\n");
    else
        printf("\nERROR: Could not save data. Check folder permissions.\n");
}


/* ---------- LOOKUP HELPERS ---------- */

int find_book(int id)
{
    for (int i = 0; i < nbooks; i++)
        if (books[i].id == id) return i;
    return -1;
}

int find_student(int id)
{
    for (int i = 0; i < nstudents; i++)
        if (students[i].id == id) return i;
    return -1;
}

int find_loan(int id)
{
    for (int i = 0; i < nloans; i++)
        if (loans[i].loan_id == id) return i;
    return -1;
}

/* number of copies of this book currently lent out */
int count_issued(int id)
{
    int count = 0;
    for (int i = 0; i < nloans; i++)
        if (loans[i].book_id == id && !loans[i].returned) count++;
    return count;
}

int copies_available(const Book *b)
{
    return b->copies - count_issued(b->id);
}

int student_has_book(int student_id, int book_id)
{
    for (int i = 0; i < nloans; i++)
        if (loans[i].student_id == student_id
            && loans[i].book_id == book_id && !loans[i].returned) return 1;
    return 0;
}

int student_has_unpaid_fine(int id)
{
    for (int i = 0; i < nloans; i++)
        if (loans[i].student_id == id && loans[i].returned
            && loans[i].fine > 0 && !loans[i].paid) return 1;
    return 0;
}

int next_loan_id(void)
{
    int max = 0;
    for (int i = 0; i < nloans; i++)
        if (loans[i].loan_id > max) max = loans[i].loan_id;
    return max + 1;
}


/* ---------- BOOK FUNCTIONS ---------- */

void print_book(const Book *b)
{
    printf("\nBook ID : %d\n", b->id);
    printf("Title   : %s\n", b->title);
    printf("Author  : %s\n", b->author);
    printf("Total Copies : %d\n", b->copies);
    printf("Available    : %d\n", copies_available(b));
    printf("Issued       : %d\n", count_issued(b->id));
    printf("Status       : %s\n",
           copies_available(b) > 0 ? "AVAILABLE" : "ALL COPIES ISSUED");
}

void add_book(void)
{
    Book b;

    printf("\n===== ADD BOOK =====\n");

    b.id = read_id("Enter Book ID: ");

    int existing = find_book(b.id);

    if (existing >= 0)
    {
        printf("Book ID already exists: %s (copies: %d)\n",
               books[existing].title, books[existing].copies);

        int extra = read_int("Enter number of copies to add (0 to cancel): ");

        if (extra == 0)
        {
            printf("Cancelled.\n");
            return;
        }

        books[existing].copies += extra;
        dirty = 1;

        if (save_all())
            printf("\nCopies added. Total copies now: %d\n",
                   books[existing].copies);
        else
            printf("\nCopies added, but SAVE FAILED. Use 'Save Data' later.\n");
        return;
    }

    read_text("Enter Book Name: ", b.title, sizeof(b.title));
    read_text("Enter Author Name: ", b.author, sizeof(b.author));

    do
        b.copies = read_int("Enter Number of Copies: ");
    while (b.copies < 1 && printf("Copies must be at least 1.\n"));

    books = grow(books, nbooks, &capbooks, sizeof(Book));
    books[nbooks++] = b;
    dirty = 1;

    if (save_all())
        printf("\nBook added and saved successfully!\n");
    else
        printf("\nBook added, but SAVE FAILED. Use 'Save Data' later.\n");
}

void view_books(void)
{
    printf("\n========== BOOK LIST ==========\n");

    if (nbooks == 0)
    {
        printf("No books found.\n");
        return;
    }

    int total = 0, issued = 0;

    for (int i = 0; i < nbooks; i++)
    {
        print_book(&books[i]);
        total  += books[i].copies;
        issued += count_issued(books[i].id);
    }

    printf("\n-----------------------------------\n");
    printf("Total Book Titles : %d\n", nbooks);
    printf("Total Copies      : %d\n", total);
    printf("Copies Issued     : %d\n", issued);
    printf("Copies Available  : %d\n", total - issued);
}

void search_book(void)
{
    int id = read_int("\nEnter Book ID to search: ");
    int i = find_book(id);

    if (i < 0)
        printf("Book not found!\n");
    else
        print_book(&books[i]);
}


/* ---------- STUDENT FUNCTIONS ---------- */

void add_student(void)
{
    Student s;

    printf("\n===== STUDENT REGISTRATION =====\n");

    s.id = read_id("Enter Student ID: ");

    if (find_student(s.id) >= 0)
    {
        printf("Student ID already exists!\n");
        return;
    }

    read_text("Enter Student Name: ", s.name, sizeof(s.name));
    read_text("Enter Department: ", s.department, sizeof(s.department));

    students = grow(students, nstudents, &capstudents, sizeof(Student));
    students[nstudents++] = s;
    dirty = 1;

    if (save_all())
        printf("\nStudent registered and saved successfully!\n");
    else
        printf("\nStudent registered, but SAVE FAILED. Use 'Save Data' later.\n");
}

void view_students(void)
{
    printf("\n========== STUDENT LIST ==========\n");

    if (nstudents == 0)
    {
        printf("No students found.\n");
        return;
    }

    for (int i = 0; i < nstudents; i++)
    {
        printf("\nStudent ID : %d\n", students[i].id);
        printf("Name       : %s\n", students[i].name);
        printf("Department : %s\n", students[i].department);
    }
}


/* ---------- LOAN FUNCTIONS ---------- */

void issue_book(void)
{
    Loan l;

    printf("\n========== ISSUE BOOK ==========\n");

    l.student_id = read_int("Enter Student ID: ");

    if (find_student(l.student_id) < 0)
    {
        printf("Student not registered!\n");
        return;
    }

    if (student_has_unpaid_fine(l.student_id))
    {
        printf("Student has an unpaid fine. Please pay the fine first.\n");
        return;
    }

    l.book_id = read_int("Enter Book ID: ");

    if (find_book(l.book_id) < 0)
    {
        printf("Book not found!\n");
        return;
    }

    if (student_has_book(l.student_id, l.book_id))
    {
        printf("This student already has a copy of this book.\n");
        return;
    }

    if (copies_available(&books[find_book(l.book_id)]) <= 0)
    {
        printf("No copies available! All copies are issued.\n");
        return;
    }

    l.loan_id = next_loan_id();
    l.issue_date = today();
    l.due_date = calculate_due_date(l.issue_date);
    l.return_date = 0;
    l.fine = 0;
    l.paid = 0;
    l.returned = 0;

    loans = grow(loans, nloans, &caploans, sizeof(Loan));
    loans[nloans++] = l;
    dirty = 1;

    int ok = save_all();

    printf("\nBook issued successfully!\n");
    printf("Loan ID          : %d\n", l.loan_id);

    printf("Issue Date       : ");
    display_date(l.issue_date);

    printf("\nReturn Due Date  : ");
    display_date(l.due_date);

    printf("\nReturn Period    : %d Days\n", RETURN_DAYS);

    if (!ok)
        printf("WARNING: SAVE FAILED. Use 'Save Data' later.\n");
}

void return_book(void)
{
    int id = read_int("\nEnter Loan ID: ");
    int i = find_loan(id);

    if (i < 0)
    {
        printf("Loan ID not found!\n");
        return;
    }

    Loan *l = &loans[i];

    if (l->returned)
    {
        printf("This book has already been returned.\n");
        return;
    }

    l->return_date = today();

    long late = date_number(l->return_date) - date_number(l->due_date);
    l->fine = late > 0 ? (int)late * FINE_PER_DAY : 0;
    l->returned = 1;
    dirty = 1;

    printf("\n========== RETURN DETAILS ==========\n");
    printf("Actual Return Date : ");
    display_date(l->return_date);
    printf("\nLate Days          : %ld\n", late > 0 ? late : 0);
    printf("Fine Per Day       : Rs.%d\n", FINE_PER_DAY);
    printf("Total Fine         : Rs.%d\n", l->fine);

    if (l->fine > 0)
    {
        printf("Amount To Pay      : Rs.%d\n", l->fine);
        printf("Payment Status     : PENDING\n");
    }
    else
    {
        printf("Amount To Pay      : Rs.0\n");
        printf("Payment Status     : NO FINE\n");
    }

    if (!save_all())
        printf("WARNING: SAVE FAILED. Use 'Save Data' later.\n");
}

void pay_fine(void)
{
    int id = read_int("\nEnter Loan ID: ");
    int i = find_loan(id);

    if (i < 0)
    {
        printf("Loan ID not found!\n");
        return;
    }

    Loan *l = &loans[i];

    if (!l->returned)
    {
        printf("Please return the book first.\n");
    }
    else if (l->fine == 0)
    {
        printf("No fine payment required.\n");
    }
    else if (l->paid)
    {
        printf("Fine already paid!\n");
    }
    else
    {
        printf("\nFine Amount: Rs.%d\n", l->fine);

        if (read_int("Enter 1 to confirm payment: ") == 1)
        {
            l->paid = 1;
            dirty = 1;
            printf("Payment successful!\n");
            printf("Amount Paid: Rs.%d\n", l->fine);

            if (!save_all())
                printf("WARNING: SAVE FAILED. Use 'Save Data' later.\n");
        }
        else
        {
            printf("Payment cancelled.\n");
        }
    }
}


/* ---------- TRANSACTION HISTORY ---------- */

void transaction_history(void)
{
    printf("\n========== TRANSACTION HISTORY ==========\n");

    if (nloans == 0)
    {
        printf("No transactions found.\n");
        return;
    }

    for (int i = 0; i < nloans; i++)
    {
        Loan *l = &loans[i];

        printf("\nLoan ID    : %d\n", l->loan_id);
        printf("Book ID    : %d\n", l->book_id);
        printf("Student ID : %d\n", l->student_id);

        printf("Issue Date : ");
        display_date(l->issue_date);

        printf("\nDue Date   : ");
        display_date(l->due_date);

        printf("\nStatus     : %s\n",
               l->returned ? "RETURNED" : "NOT RETURNED");

        if (l->returned)
        {
            printf("Return Date: ");
            display_date(l->return_date);

            printf("\nFine       : Rs.%d\n", l->fine);
            printf("Payment    : %s\n",
                   l->paid ? "PAID" :
                   l->fine > 0 ? "PENDING" : "NOT REQUIRED");
        }

        printf("-----------------------------------\n");
    }
}


/* ---------- MAIN MENU ---------- */

int main(void)
{
    int choice;

    load_all();
    printf("Loaded %d book(s), %d student(s), %d loan(s).\n",
           nbooks, nstudents, nloans);

    while (1)
    {
        printf("\n\n");
        printf("========================================\n");
        printf("       LIBRARY MANAGEMENT SYSTEM\n");
        printf("========================================\n");

        printf("1.  Add Book\n");
        printf("2.  View Books\n");
        printf("3.  Search Book\n");
        printf("4.  Register Student\n");
        printf("5.  View Students\n");
        printf("6.  Issue Book\n");
        printf("7.  Return Book\n");
        printf("8.  Transaction History\n");
        printf("9.  Pay Fine\n");
        printf("10. Save Data\n");
        printf("0.  Save and Exit\n");

        printf("========================================\n");

        choice = read_int("Enter Your Choice: ");

        switch (choice)
        {
            case 1:  add_book();            break;
            case 2:  view_books();          break;
            case 3:  search_book();         break;
            case 4:  add_student();         break;
            case 5:  view_students();       break;
            case 6:  issue_book();          break;
            case 7:  return_book();         break;
            case 8:  transaction_history(); break;
            case 9:  pay_fine();            break;
            case 10: save_menu();           break;

            case 0:
                if (save_all())
                {
                    printf("\nData saved. Thank you for using "
                           "Library Management System!\n");
                    free(books);
                    free(students);
                    free(loans);
                    return 0;
                }
                printf("\nERROR: Could not save data, so the program "
                       "will not exit.\nFix the problem and try again.\n");
                break;

            default:
                printf("\nInvalid choice! Try again.\n");
        }
    }
}
