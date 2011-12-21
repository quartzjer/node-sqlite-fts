var sqlite = require('./sqlite-fts');

var db = new sqlite.Database();

// open the database for reading if file exists
// create new database file if not

db.open("aquateen.db", function (error) {
  if (error) {
      console.log("Tonight. You.");
      throw error;
  }
  db.executeScript
    (   "CREATE TABLE IF NOT EXISTS table1 (id, name);"
      + "CREATE TABLE IF NOT EXISTS table2 (id, age);"
      + "CREATE VIRTUAL TABLE table3 USING fts4(name TEXT);"
      + "INSERT INTO table1 VALUES (1, 'Mister Shake');"
      + "INSERT INTO table2 VALUES (1, 34);"
    , function (error) {
        if (error) throw error;
        db.prepare("SELECT * FROM table1", function(err, statement){
            if(err) throw error;
            statement.fetchAll(function (error, rows) {
              if (error) throw error;
              console.dir(rows);
            });
        });
      });
});