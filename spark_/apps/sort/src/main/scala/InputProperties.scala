package PowerMeasurements

import org.apache.spark.{SparkConf, SparkContext}
import org.apache.spark.rdd.RDD


// sbt build issues: https://stackoverflow.com/questions/45531198/warnings-while-building-scala-spark-project-with-sbt
/**
  * To evaluate input dataset and its properties
  */
object InputProperties {

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

    var raw: RDD[Array[Byte]] = sc.binaryRecords(inputPath,100)
    var dataset: RDD[(Array[Byte],Array[Byte])] = raw.map(line => (line.slice(0,10), line.slice(10,100)))
    dataset.setName("InputRDD")

    raw.unpersist()
    raw=null
    // dataset.persist()

    // Do not write output to disk, just call an "action" to kick off RDD computation
    println("Spark input count: " + dataset.count())

    // Print input partitions
    println("Input partitioning")
    // dataset.map(kv => (TeraSortPartitioner(745).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).collect().foreach(println)
    println("100    : " + dataset.map(kv => (TeraSortPartitioner(100).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())
    println("500    : " + dataset.map(kv => (TeraSortPartitioner(500).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())
    println("1000   : " + dataset.map(kv => (TeraSortPartitioner(1000).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())
    println("5000   : " + dataset.map(kv => (TeraSortPartitioner(5000).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())
    println("10000  : " + dataset.map(kv => (TeraSortPartitioner(10000).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())
    println("100000 : " + dataset.map(kv => (TeraSortPartitioner(100000).getPartition(kv._1), 1)).reduceByKey((x, y) => x + y).map(_._2).stats())

    // Print size of partitions
    // sorted.mapPartitions(iter => Array(iter.size).iterator, true).collect().foreach(println)

    // To keep the spark job alive for checking things on Web UI
    // Thread.sleep(60000)
    sc.stop()
  }
}

