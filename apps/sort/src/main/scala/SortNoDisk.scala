package PowerMeasurements

import org.apache.spark.SparkFiles
import org.apache.spark.rdd.RDD
import org.apache.spark.SparkContext
import org.apache.spark.SparkConf


// sbt build issues: https://stackoverflow.com/questions/45531198/warnings-while-building-scala-spark-project-with-sbt
/**
  * Sort using Spark Range Partitioner, does not write output to disk
  */
object SortNoDisk {

  def main(args: Array[String]): Unit = {
    val inputPath = args(1)

    val conf = new SparkConf().
      setMaster(args(0)).
      setAppName("SortNoDisk")

    val sc = new SparkContext(conf)
    // sc.setLogLevel("DEBUG")

    //https://stackoverflow.com/questions/7109943/how-to-define-orderingarraybyte
    implicit val ordering = new math.Ordering[Array[Byte]] {
      def compare(a: Array[Byte], b: Array[Byte]): Int = {
        if (a eq null) {
          if (b eq null) 0
          else -1
        }
        else if (b eq null) 1
        else {
          val L = math.min(a.length, b.length)
          var i = 0
          while (i < L) {
            if (a(i) < b(i)) return -1
            else if (b(i) < a(i)) return 1
            i += 1
          }
          if (L < b.length) -1
          else if (L < a.length) 1
          else 0
        }
      }
    }

    println("===================== Spark Conf: Begin ==========================")
    println(conf.toDebugString)
    println("===================== Spark Conf: End ==========================")

    // val taskMetrics = ch.cern.sparkmeasure.TaskMetrics(spark)
    // taskMetrics.begin()

    var text_RDD: RDD[Array[Byte]] = sc.binaryRecords(inputPath,100)
    var kv_RDD: RDD[(Array[Byte],Array[Byte])] = 
      text_RDD.map(line => {  
          // (line.slice(0,10), line.slice(10,100))
          var temp = Array.fill[Byte](450)(0)
          val x = line.slice(0,10)
          var temp2 = Array.fill[Byte](50)(0)
          val y = line.slice(10,100)
          (x, y)
        })
    kv_RDD.setName("InputRDD")

    text_RDD.unpersist()
    text_RDD=null
    // kv_RDD.persist()

    var sorted = kv_RDD.sortBy(_._1, true)
    sorted.setName("SortedRDD")
    // sorted.persist()

    // Do not write output to disk, just call an "action" to kick off RDD computation
    println("Spark output count: " + sorted.count())

    // Print size of partitions
    // sorted.mapPartitions(iter => Array(iter.size).iterator, true).collect().foreach(println)

    // To keep the spark job alive for checking things on Web UI
    // Thread.sleep(60000)
    sc.stop()
  }
}

