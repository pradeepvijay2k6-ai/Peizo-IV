/*
 * Piezo IV - Hotel Takeaway Management System
 * C Backend — UPDATED
 *
 * Changes from original:
 *   1. UNIFIED PRIORITY QUEUE (single min-heap) — no separate normal Queue.
 *      Priority key = (is_priority ? 0 : 1) * 1000000 + token
 *      Priority orders (<=3 items) always sort before normal orders.
 *      Within same tier, lower token (earlier arrival) comes first.
 *
 *   2. NOTIFICATION STACK — replaced singly linked list with an explicit
 *      stack (array-backed).  push/pop/peek in O(1).
 *
 *   3. BEST-SELLER from BST — bst_find_best() traverses the BST and
 *      returns the node with highest qty_sold.  Exported via get_best_seller().
 *
 * All other logic unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════
   CONSTANTS
═══════════════════════════════════════════ */
#define MAX_MENU            20
#define MAX_ORDERS          500
#define MAX_ITEMS_PER_ORDER 20
#define MAX_NAME            64
#define MAX_PHONE           16
#define MAX_MSG             256
#define PRIORITY_THRESHOLD  3

/* ═══════════════════════════════════════════
   STRUCTS
═══════════════════════════════════════════ */

typedef struct {
    int  id;
    char name[MAX_NAME];
    int  price;
    char category[MAX_NAME];
    char image_url[256];
    int  available;   /* 1 = available, 0 = unavailable */
} MenuItem;

typedef struct {
    int item_id;
    int quantity;
} OrderItem;

/* Order statuses: 0=Preparing, 1=Ready, 2=Collected */
typedef struct {
    int       token;
    char      customer_name[MAX_NAME];
    char      phone[MAX_PHONE];
    OrderItem items[MAX_ITEMS_PER_ORDER];
    int       item_count;
    int       total_price;
    int       total_qty;
    int       is_priority;
    int       status;        /* 0=Preparing, 1=Ready, 2=Collected */
    char      time_str[32];
} Order;

/* ─────────────────────────────────────────────────────────
   CHANGE 1 : UNIFIED PRIORITY QUEUE (single min-heap)
   Each entry stores a composite key:
       key = (is_priority ? 0 : 1) * 1 000 000 + token
   Priority orders have key < 1 000 000, normal ones >= 1 000 000.
   Within the same tier, smaller token == earlier arrival → first out.
   ───────────────────────────────────────────────────────── */
#define UPQ_MAX 500

typedef struct {
    int key;    /* composite sort key  */
    int token;  /* actual order token  */
} UPQEntry;

typedef struct {
    UPQEntry entries[UPQ_MAX];
    int      size;
} UnifiedPQ;

/* ─────────────────────────────────────────────────────────
   CHANGE 2 : NOTIFICATION STACK (array-backed)
   ───────────────────────────────────────────────────────── */
#define NOTIF_STACK_MAX 1000

typedef struct {
    int  order_token;
    char message[MAX_MSG];
    char time_str[32];
    int  type;   /* 0=confirm, 1=ready, 2=system */
} NotifEntry;

typedef struct {
    NotifEntry items[NOTIF_STACK_MAX];
    int        top;   /* index of next free slot; -1 = empty */
} NotifStack;

/* ─────────────────────────────────────────────────────────
   BST Node for sales analytics (keyed by item_id)
   ───────────────────────────────────────────────────────── */
typedef struct BSTNode {
    int            item_id;
    char           item_name[MAX_NAME];
    int            qty_sold;
    int            revenue;
    struct BSTNode *left;
    struct BSTNode *right;
} BSTNode;

/* ═══════════════════════════════════════════
   GLOBAL STATE  (in-memory only)
═══════════════════════════════════════════ */

static MenuItem  menu[MAX_MENU];
static int       menu_size    = 0;

static Order     orders[MAX_ORDERS];
static int       order_count  = 0;
static int       token_counter = 0;

static UnifiedPQ upq;           /* CHANGE 1 — single priority queue */
static NotifStack notif_stack;  /* CHANGE 2 — notification stack    */
static BSTNode  *bst_root = NULL;

/* ═══════════════════════════════════════════
   CHANGE 1 — UNIFIED PRIORITY QUEUE OPERATIONS
═══════════════════════════════════════════ */

/* Build sort key: priority orders rank before normal orders;
   within the same tier, lower token number comes first. */
static int upq_make_key(int token, int is_priority) {
    return (is_priority ? 0 : 1) * 1000000 + token;
}

static void upq_swap(UnifiedPQ *q, int a, int b) {
    UPQEntry tmp = q->entries[a];
    q->entries[a] = q->entries[b];
    q->entries[b] = tmp;
}

static void upq_sift_up(UnifiedPQ *q, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (q->entries[p].key > q->entries[i].key) {
            upq_swap(q, p, i);
            i = p;
        } else break;
    }
}

static void upq_sift_down(UnifiedPQ *q, int i) {
    while (1) {
        int smallest = i;
        int l = 2 * i + 1, r = 2 * i + 2;
        if (l < q->size && q->entries[l].key < q->entries[smallest].key) smallest = l;
        if (r < q->size && q->entries[r].key < q->entries[smallest].key) smallest = r;
        if (smallest == i) break;
        upq_swap(q, i, smallest);
        i = smallest;
    }
}

static void upq_push(UnifiedPQ *q, int token, int is_priority) {
    if (q->size >= UPQ_MAX) return;
    q->entries[q->size].token = token;
    q->entries[q->size].key   = upq_make_key(token, is_priority);
    upq_sift_up(q, q->size);
    q->size++;
}

/* Remove a specific token from the heap (e.g. when order becomes Ready) */
static void upq_remove(UnifiedPQ *q, int token) {
    int idx = -1;
    for (int i = 0; i < q->size; i++) {
        if (q->entries[i].token == token) { idx = i; break; }
    }
    if (idx < 0) return;
    q->size--;
    if (idx < q->size) {
        q->entries[idx] = q->entries[q->size];
        upq_sift_up(q, idx);
        upq_sift_down(q, idx);
    }
}

/* ═══════════════════════════════════════════
   CHANGE 2 — NOTIFICATION STACK OPERATIONS
═══════════════════════════════════════════ */

static void notif_stack_init(NotifStack *s) {
    s->top = -1;
}

/* Push a new notification onto the stack (newest on top) */
static void notif_push(NotifStack *s, int token, const char *msg,
                        int type, const char *time_str) {
    if (s->top >= NOTIF_STACK_MAX - 1) return;   /* stack full — drop oldest push first if needed */
    s->top++;
    s->items[s->top].order_token = token;
    strncpy(s->items[s->top].message,  msg,      MAX_MSG - 1);
    strncpy(s->items[s->top].time_str, time_str, 31);
    s->items[s->top].type = type;
}

/* Peek at top without popping */
static NotifEntry *notif_peek(NotifStack *s) {
    if (s->top < 0) return NULL;
    return &s->items[s->top];
}

/* Pop and discard top (unused externally but part of the stack ADT) */
/* static void notif_pop(NotifStack *s) { if (s->top >= 0) s->top--; } */

/* ═══════════════════════════════════════════
   BST OPERATIONS (keyed by item_id)
═══════════════════════════════════════════ */

static BSTNode *bst_insert(BSTNode *root, int item_id, const char *name,
                            int qty, int rev) {
    if (!root) {
        BSTNode *n   = (BSTNode *)calloc(1, sizeof(BSTNode));
        n->item_id   = item_id;
        strncpy(n->item_name, name, MAX_NAME - 1);
        n->qty_sold  = qty;
        n->revenue   = rev;
        return n;
    }
    if (item_id < root->item_id)
        root->left  = bst_insert(root->left,  item_id, name, qty, rev);
    else if (item_id > root->item_id)
        root->right = bst_insert(root->right, item_id, name, qty, rev);
    else {
        root->qty_sold += qty;
        root->revenue  += rev;
    }
    return root;
}

/* In-order traversal — writes JSON array entries into a shared buffer */
static char bst_buf[65536];
static int  bst_pos;

static void bst_inorder(BSTNode *root) {
    if (!root) return;
    bst_inorder(root->left);
    if (bst_pos > 0)
        bst_pos += snprintf(bst_buf + bst_pos, sizeof(bst_buf) - bst_pos, ",");
    bst_pos += snprintf(bst_buf + bst_pos, sizeof(bst_buf) - bst_pos,
        "{\"item_id\":%d,\"item_name\":\"%s\",\"qty_sold\":%d,\"revenue\":%d}",
        root->item_id, root->item_name, root->qty_sold, root->revenue);
    bst_inorder(root->right);
}

/* CHANGE 3 — Find best-selling node (highest qty_sold) via full traversal */
static BSTNode *bst_find_best(BSTNode *root) {
    if (!root) return NULL;
    BSTNode *best       = root;
    BSTNode *left_best  = bst_find_best(root->left);
    BSTNode *right_best = bst_find_best(root->right);
    if (left_best  && left_best->qty_sold  > best->qty_sold) best = left_best;
    if (right_best && right_best->qty_sold > best->qty_sold) best = right_best;
    return best;
}

/* ═══════════════════════════════════════════
   MENU INITIALISATION
═══════════════════════════════════════════ */

static void init_menu() {
    MenuItem m[] = {
        {1, "Paneer Tikka",        120, "Starters",    "https://images.unsplash.com/photo-1567188040759-fb8a883dc6d8?w=400&q=80",  1},
        {2, "Chicken 65",          140, "Starters",    "https://images.unsplash.com/photo-1610057099443-fde8c4d50f91?w=400&q=80",  1},
        {3, "Veg Spring Rolls",     80, "Starters",    "https://images.unsplash.com/photo-1540420773420-3366772f4999?w=400&q=80",  1},
        {4, "Fish Fry",            160, "Starters",    "https://images.unsplash.com/photo-1519708227418-c8fd9a32b7a2?w=400&q=80",  1},
        {5, "Veg Biryani",         120, "Main Course", "https://images.unsplash.com/photo-1563379091339-03b21ab4a4f8?w=400&q=80",  1},
        {6, "Chicken Biryani",     160, "Main Course", "https://images.unsplash.com/photo-1589302168068-964664d93dc0?w=400&q=80",  1},
        {7, "Mutton Biryani",      200, "Main Course", "https://images.unsplash.com/photo-1633945274405-b6c8069047b0?w=400&q=80",  1},
        {8, "Dal Tadka",            90, "Main Course", "https://images.unsplash.com/photo-1546833999-b9f581a1996d?w=400&q=80",  1},
        {9, "Butter Naan",          30, "Main Course", "https://images.unsplash.com/photo-1604152135912-04a022e23696?w=400&q=80",  1},
        {10,"Paneer Butter Masala",150, "Main Course", "https://images.unsplash.com/photo-1631452180519-c014fe946bc7?w=400&q=80",  1},
        {11,"Chicken Curry",       170, "Main Course", "https://images.unsplash.com/photo-1603894584373-5ac82b2ae398?w=400&q=80",  1},
        {12,"Fried Rice",          110, "Main Course", "https://images.unsplash.com/photo-1603133872878-684f208fb84b?w=400&q=80",  1},
        {13,"Gulab Jamun",          50, "Desserts",    "https://images.unsplash.com/photo-1593701461250-d7b22dfd3a77?w=400&q=80",  1},
        {14,"Ice Cream",            60, "Desserts",    "https://images.unsplash.com/photo-1563805042-7684c019e1cb?w=400&q=80",  1},
        {15,"Rasmalai",             70, "Desserts",    "https://images.unsplash.com/photo-1601050690597-df0568f70950?w=400&q=80",  1},
        {16,"Masala Chai",          30, "Beverages",   "https://images.unsplash.com/photo-1571934811356-5cc061b6821f?w=400&q=80",  1},
        {17,"Mango Lassi",          60, "Beverages",   "https://images.unsplash.com/photo-1571091718767-18b5b1457add?w=400&q=80",  1},
        {18,"Fresh Lime Soda",      40, "Beverages",   "https://images.unsplash.com/photo-1556679343-c7306c1976bc?w=400&q=80",  1},
    };
    menu_size = (int)(sizeof(m) / sizeof(m[0]));
    for (int i = 0; i < menu_size; i++) menu[i] = m[i];
}

/* ═══════════════════════════════════════════
   EXPORTED API  (called via ctypes from Flask)
═══════════════════════════════════════════ */

/* Helper: escape double-quotes in a string for JSON */
static void json_str(char *out, int osz, const char *in) {
    int j = 0;
    for (int i = 0; in[i] && j < osz - 2; i++) {
        if (in[i] == '"') { out[j++] = '\\'; out[j++] = '"'; }
        else out[j++] = in[i];
    }
    out[j] = 0;
}

/* ── INIT ── */
void __attribute__((constructor)) lib_init() {
    init_menu();
    upq.size = 0;                /* CHANGE 1 — single queue, no separate normal_queue */
    notif_stack_init(&notif_stack); /* CHANGE 2 — stack init */
    order_count   = 0;
    token_counter = 0;
}

/* ── GET MENU ──
   Returns JSON array of all menu items */
void get_menu(char *out, int out_size) {
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos, "[");
    for (int i = 0; i < menu_size; i++) {
        if (i) pos += snprintf(out + pos, out_size - pos, ",");
        char ename[MAX_NAME * 2], ecat[MAX_NAME * 2];
        json_str(ename, sizeof(ename), menu[i].name);
        json_str(ecat,  sizeof(ecat),  menu[i].category);
        pos += snprintf(out + pos, out_size - pos,
            "{\"id\":%d,\"name\":\"%s\",\"price\":%d,\"category\":\"%s\","
            "\"image_url\":\"%s\",\"available\":%d}",
            menu[i].id, ename, menu[i].price, ecat,
            menu[i].image_url, menu[i].available);
    }
    pos += snprintf(out + pos, out_size - pos, "]");
}

/* ── PLACE ORDER ──
   items_json: "[{\"item_id\":1,\"quantity\":2}, ...]"
   customer_name, phone, time_str
   Returns JSON with token, total, is_priority */
void place_order(const char *items_json, const char *customer_name,
                 const char *phone,      const char *time_str,
                 char *out, int out_size) {
    if (order_count >= MAX_ORDERS) {
        snprintf(out, out_size, "{\"error\":\"Order queue full\"}");
        return;
    }

    Order *o = &orders[order_count];
    memset(o, 0, sizeof(Order));
    token_counter++;
    o->token = token_counter;
    strncpy(o->customer_name, customer_name, MAX_NAME  - 1);
    strncpy(o->phone,         phone,         MAX_PHONE - 1);
    strncpy(o->time_str,      time_str,      31);
    o->status = 0;   /* Preparing */

    /* Parse items_json: [{\"item_id\":N,\"quantity\":M}, ...] */
    int item_count = 0, total_price = 0, total_qty = 0;
    const char *p = items_json;
    while (*p) {
        int iid = 0, qty = 0;
        char *f = strstr(p, "\"item_id\":");
        if (!f) break;
        f += 10; iid = atoi(f);
        char *g = strstr(f, "\"quantity\":");
        if (!g) break;
        g += 11; qty = atoi(g);
        if (qty <= 0) { p = g + 1; continue; }

        MenuItem *mi = NULL;
        for (int i = 0; i < menu_size; i++)
            if (menu[i].id == iid && menu[i].available) { mi = &menu[i]; break; }
        if (mi) {
            o->items[item_count].item_id  = iid;
            o->items[item_count].quantity = qty;
            item_count++;
            total_price += mi->price * qty;
            total_qty   += qty;
            /* Update BST */
            bst_root = bst_insert(bst_root, iid, mi->name, qty, mi->price * qty);
        }
        p = g + 1;
        if (item_count >= MAX_ITEMS_PER_ORDER) break;
    }

    o->item_count  = item_count;
    o->total_price = total_price;
    o->total_qty   = total_qty;
    o->is_priority = (total_qty <= PRIORITY_THRESHOLD) ? 1 : 0;

    /* CHANGE 1 — push into single unified priority queue */
    upq_push(&upq, o->token, o->is_priority);

    /* CHANGE 2 — push confirmation onto notification stack */
    char msg[MAX_MSG];
    snprintf(msg, MAX_MSG,
        "Order confirmed! Token T%03d · Total Rs.%d · %s",
        o->token, o->total_price,
        o->is_priority ? "Priority order" : "Preparing now");
    notif_push(&notif_stack, o->token, msg, 0, time_str);

    order_count++;

    char token_str[8];
    snprintf(token_str, 8, "T%03d", o->token);
    snprintf(out, out_size,
        "{\"token\":%d,\"token_str\":\"%s\",\"total\":%d,"
        "\"is_priority\":%d,\"total_qty\":%d}",
        o->token, token_str, o->total_price, o->is_priority, o->total_qty);
}

/* ── GET ORDERS ──
   Returns JSON array of all orders (for manager) */
void get_orders(char *out, int out_size) {
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos, "[");
    for (int i = 0; i < order_count; i++) {
        Order *o = &orders[i];
        if (i) pos += snprintf(out + pos, out_size - pos, ",");
        char token_str[8];
        snprintf(token_str, 8, "T%03d", o->token);
        char cname[MAX_NAME * 2];
        json_str(cname, sizeof(cname), o->customer_name);
        pos += snprintf(out + pos, out_size - pos,
            "{\"token\":%d,\"token_str\":\"%s\",\"customer_name\":\"%s\","
            "\"phone\":\"%s\",\"total\":%d,\"is_priority\":%d,"
            "\"status\":%d,\"time_str\":\"%s\",\"items\":[",
            o->token, token_str, cname, o->phone,
            o->total_price, o->is_priority, o->status, o->time_str);
        for (int j = 0; j < o->item_count; j++) {
            if (j) pos += snprintf(out + pos, out_size - pos, ",");
            const char *iname = "Unknown";
            for (int k = 0; k < menu_size; k++)
                if (menu[k].id == o->items[j].item_id) { iname = menu[k].name; break; }
            char einame[MAX_NAME * 2];
            json_str(einame, sizeof(einame), iname);
            pos += snprintf(out + pos, out_size - pos,
                "{\"item_id\":%d,\"name\":\"%s\",\"quantity\":%d}",
                o->items[j].item_id, einame, o->items[j].quantity);
        }
        pos += snprintf(out + pos, out_size - pos, "]}");
    }
    pos += snprintf(out + pos, out_size - pos, "]");
}

/* ── GET CUSTOMER ORDERS (by phone) ── */
void get_customer_orders(const char *phone, char *out, int out_size) {
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos, "[");
    int first = 1;
    for (int i = 0; i < order_count; i++) {
        Order *o = &orders[i];
        if (strcmp(o->phone, phone) != 0) continue;
        if (!first) pos += snprintf(out + pos, out_size - pos, ",");
        first = 0;
        char token_str[8];
        snprintf(token_str, 8, "T%03d", o->token);
        char cname[MAX_NAME * 2];
        json_str(cname, sizeof(cname), o->customer_name);
        pos += snprintf(out + pos, out_size - pos,
            "{\"token\":%d,\"token_str\":\"%s\",\"customer_name\":\"%s\","
            "\"total\":%d,\"is_priority\":%d,\"status\":%d,\"time_str\":\"%s\"}",
            o->token, token_str, cname,
            o->total_price, o->is_priority, o->status, o->time_str);
    }
    pos += snprintf(out + pos, out_size - pos, "]");
}

/* ── MARK READY ── */
void mark_ready(int token, const char *time_str, char *out, int out_size) {
    for (int i = 0; i < order_count; i++) {
        if (orders[i].token == token && orders[i].status == 0) {
            orders[i].status = 1;
            /* CHANGE 1 — remove from unified priority queue */
            upq_remove(&upq, token);
            /* CHANGE 2 — push "ready" notification onto stack */
            char msg[MAX_MSG];
            char token_str[8];
            snprintf(token_str, 8, "T%03d", token);
            snprintf(msg, MAX_MSG,
                "Your order %s is READY! Please collect at the counter.", token_str);
            notif_push(&notif_stack, token, msg, 1, time_str);
            snprintf(out, out_size, "{\"success\":true,\"token\":%d}", token);
            return;
        }
    }
    snprintf(out, out_size, "{\"error\":\"Order not found or not in preparing state\"}");
}

/* ── MARK COLLECTED ── */
void mark_collected(int token, char *out, int out_size) {
    for (int i = 0; i < order_count; i++) {
        if (orders[i].token == token && orders[i].status == 1) {
            orders[i].status = 2;
            snprintf(out, out_size, "{\"success\":true,\"token\":%d}", token);
            return;
        }
    }
    snprintf(out, out_size, "{\"error\":\"Order not found or not ready\"}");
}

/* ── TOGGLE AVAILABILITY ── */
void toggle_availability(int item_id, char *out, int out_size) {
    for (int i = 0; i < menu_size; i++) {
        if (menu[i].id == item_id) {
            menu[i].available = menu[i].available ? 0 : 1;
            snprintf(out, out_size,
                "{\"item_id\":%d,\"available\":%d}", item_id, menu[i].available);
            return;
        }
    }
    snprintf(out, out_size, "{\"error\":\"Item not found\"}");
}

/* ── GET NOTIFICATIONS ──
   CHANGE 2: reads from the stack (top → bottom = newest → oldest).
   Returns latest max_count notifications as JSON array. */
void get_notifications(int max_count, char *out, int out_size) {
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos, "[");
    int cnt = 0;
    /* Iterate from top of stack downward */
    for (int i = notif_stack.top; i >= 0 && cnt < max_count; i--, cnt++) {
        if (cnt) pos += snprintf(out + pos, out_size - pos, ",");
        char emsg[MAX_MSG * 2];
        json_str(emsg, sizeof(emsg), notif_stack.items[i].message);
        pos += snprintf(out + pos, out_size - pos,
            "{\"token\":%d,\"message\":\"%s\",\"time_str\":\"%s\",\"type\":%d}",
            notif_stack.items[i].order_token,
            emsg,
            notif_stack.items[i].time_str,
            notif_stack.items[i].type);
    }
    pos += snprintf(out + pos, out_size - pos, "]");
}

/* ── GET SALES (BST in-order traversal) ── */
void get_sales(char *out, int out_size) {
    bst_pos = 0;
    memset(bst_buf, 0, sizeof(bst_buf));
    bst_inorder(bst_root);
    snprintf(out, out_size, "[%s]", bst_buf);
}

/* ── GET BEST SELLER ──
   CHANGE 3: traverses BST to find the item with highest qty_sold.
   Returns JSON: {"item_id":N,"item_name":"...","qty_sold":M,"revenue":R}
   or {} if no sales yet. */
void get_best_seller(char *out, int out_size) {
    BSTNode *best = bst_find_best(bst_root);
    if (!best) {
        snprintf(out, out_size, "{}");
        return;
    }
    char ename[MAX_NAME * 2];
    json_str(ename, sizeof(ename), best->item_name);
    snprintf(out, out_size,
        "{\"item_id\":%d,\"item_name\":\"%s\",\"qty_sold\":%d,\"revenue\":%d}",
        best->item_id, ename, best->qty_sold, best->revenue);
}

/* ── GET STATS ── */
void get_stats(char *out, int out_size) {
    int preparing = 0, ready = 0, collected = 0, revenue = 0;
    for (int i = 0; i < order_count; i++) {
        if      (orders[i].status == 0) preparing++;
        else if (orders[i].status == 1) ready++;
        else                            collected++;
        revenue += orders[i].total_price;
    }
    snprintf(out, out_size,
        "{\"total\":%d,\"preparing\":%d,\"ready\":%d,"
        "\"collected\":%d,\"revenue\":%d}",
        order_count, preparing, ready, collected, revenue);
}

/* ── MANAGER LOGIN ── */
void check_manager_login(const char *username, const char *password,
                          char *out, int out_size) {
    if (strcmp(username, "manager") == 0 && strcmp(password, "piezo@123") == 0)
        snprintf(out, out_size, "{\"success\":true}");
    else
        snprintf(out, out_size,
            "{\"success\":false,\"error\":\"Invalid username or password\"}");
}
