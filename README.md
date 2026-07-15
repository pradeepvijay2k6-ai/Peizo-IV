# Piezo IV: Hotel Takeaway Management System

## Project Overview

Piezo IV is a full-stack hotel takeaway management system built to streamline restaurant takeaway services, eliminate order order delays, and handle busy rush hours. The system uses a hybrid architecture: all backend database operations and core business logic are written in pure C, compiled as a shared library, and connected to a responsive Bootstrap 5 web frontend via a lightweight Python Flask API bridge.

---

## Key Features

* **Smart Priority Queue:** Shorter orders of 3 items or less are routed to a min-heap priority queue to speed up quick orders, while larger orders are handled in standard first-in, first-out sequence.


* **BST Sales Analytics:** A Binary Search Tree structure tracks every item sold, automatically updating quantities and total revenues to instantly pinpoint the "Best Seller of the Day".


* **Live Menu Toggles:** Managers can toggle item availability on or off, and the change reflects instantly on the customer-facing menu.


* **Notification Stack:** An array-backed LIFO stack delivers immediate order status updates (Order Confirmed and Order Ready) directly to customers.


* **Manager Dashboard:** A secure login view displaying real-time revenue, active order preparation tables, and sales charts.



---

## How It Works

1. **Frontend:** The customer browsing portal and manager dashboard are built with HTML, CSS, and Bootstrap 5. All actions communicate with the server using standard JavaScript fetch requests.


2. **Bridge:** A simple Python Flask application acts as a pass-through layer. It uses the Python ctypes library to invoke backend functions directly from the compiled C code.


3. **Backend:** A compiled C library handles the data structures (Menu Array, Priority Heap, Notification Stack, Sales BST) and contains the core business logic with zero external dependencies.



---

## Core Data Structures

* **Static Array:** Used for Menu Storage to ensure instant lookup times without memory allocation overhead.


* **Min-Heap Queue:** Used for Order Scheduling to surface quick orders first with log-time performance.


* **Array-backed Stack:** Used for Notifications to naturally return the most recent updates at the top.


* **Binary Search Tree:** Used for Sales Analytics to group transactions by item ID and provide sorted sales reports.



---

## Setup and Installation

### 1. Clone the Code

```bash
git clone https://github.com/preetha2510861-ap/hotel-system.git
cd hotel-system

```

### 2. Compile the C Backend

Compile the backend C file into a shared library:

* On Windows:

```bash
gcc -shared -o backend.dll backend.c

```

* On Linux or macOS:

```bash
gcc -shared -o backend.so -fPIC backend.c

```

### 3. Start the Server

Install Flask and launch the API bridge:

```bash
pip install flask
python app.py

```

Go to [http://127.0.0.1:5000](https://www.google.com/search?q=http://127.0.0.1:5000) in your web browser.

---

## Development Team

* **Preetha A:** Order Queue Management and ctypes library integration.


* **Pranaya Shree S:Frontend Development, Manager dashboard stats, and status updates.


* **Pavithra SSM:** Fixed Menu Array, API integration, and sorted sales reports.


* **Pradeep V:** Bootstrap frontend design, interactive shopping cart, Min-Heap Logic, LIFO Notification Stack and Sales BST implementation.
