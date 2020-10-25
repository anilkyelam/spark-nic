/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package PowerMeasurements

import com.google.common.primitives.Longs
import org.apache.spark.Partitioner

/**
 * Partitioner for terasort. It uses the first seven bytes of the byte array to partition
 * the key space evenly.
 */
case class TeraSortLongPartitioner(numPartitions: Int) extends Partitioner {

  import TeraSortLongPartitioner._

  val rangePerPart : Long = (max - min) / numPartitions

  override def getPartition(key: Any): Int = {
    val b = key.asInstanceOf[Long]
    (b / rangePerPart).toInt
  }
}

object TeraSortLongPartitioner {
  val min : Long = Longs.fromBytes(0, 0, 0, 0, 0, 0, 0, 0)
  val max : Long = Longs.fromBytes(0, -1, -1, -1, -1, -1, -1, -1)  // 0xff = -1
}
